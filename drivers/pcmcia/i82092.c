/* 
 * Driver for Intel I82092AA PCI-PCMCIA bridge.
 *
 * (C) 2001 Red Hat, Inc.
 *
 * Author: Arjan Van De Ven <arjanv@redhat.com>
 * Loosly based on i82365.c from the pcmcia-cs package
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <pcmcia/ss.h>
#include <pcmcia/cs.h>

#include <asm/system.h>
#include <asm/io.h>

#include "i82092aa.h"
#include "i82365.h"

MODULE_LICENSE("GPL");

/* PCI core routines */
static struct pci_device_id i82092aa_pci_ids[] = {
	{
	      .vendor = PCI_VENDOR_ID_INTEL,
	      .device = PCI_DEVICE_ID_INTEL_82092AA_0,
	      .subvendor = PCI_ANY_ID,
	      .subdevice = PCI_ANY_ID,
	 },
	 {} 
};
MODULE_DEVICE_TABLE(pci, i82092aa_pci_ids);

static struct pci_driver i82092aa_pci_driver = {
	.name           = "i82092aa",
	.id_table       = i82092aa_pci_ids,
	.probe          = i82092aa_pci_probe,
	.remove         = __devexit_p(i82092aa_pci_remove),
};


/* the pccard structure and its functions */
static struct pccard_operations i82092aa_operations = {
	.init 		 	= i82092aa_init,
	.get_status		= i82092aa_get_status,
	.set_socket		= i82092aa_set_socket,
	.set_io_map		= i82092aa_set_io_map,
	.set_mem_map		= i82092aa_set_mem_map,
};

/* The card can do upto 4 sockets, allocate a structure for each of them */

struct socket_info {
	int	number;
	int	card_state; 	/*  0 = no socket,
				    1 = empty socket, 
				    2 = card but not initialized,
				    3 = operational card */
	unsigned int io_base; 	/* base io address of the socket */
	
	struct pcmcia_socket socket;
	struct pci_dev *dev;	/* The PCI device for the socket */
};

#define MAX_SOCKETS 4
static struct socket_info sockets[MAX_SOCKETS];
static int socket_count;  /* shortcut */                                  	                                	


static int __devinit i82092aa_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	unsigned char configbyte;
	int i, ret;
	
	enter("i82092aa_pci_probe");
	
	if ((ret = pci_enable_device(dev)))
		return ret;
		
	pci_read_config_byte(dev, 0x40, &configbyte);  /* PCI Configuration Control */
	switch(configbyte&6) {
		case 0:
			socket_count = 2;
			break;
		case 2:
			socket_count = 1;
			break;
		case 4:
		case 6:
			socket_count = 4;
			break;
			
		default:
			printk(KERN_ERR "i82092aa: Oops, you did something we didn't think of.\n");
			ret = -EIO;
			goto err_out_disable;
	}
	printk(KERN_INFO "i82092aa: configured as a %d socket device.\n", socket_count);

	if (!request_region(pci_resource_start(dev, 0), 2, "i82092aa")) {
		ret = -EBUSY;
		goto err_out_disable;
	}
	
	for (i = 0;i<socket_count;i++) {
		sockets[i].card_state = 1; /* 1 = present but empty */
		sockets[i].io_base = pci_resource_start(dev, 0);
		sockets[i].socket.features |= SS_CAP_PCCARD;
		sockets[i].socket.map_size = 0x1000;
		sockets[i].socket.irq_mask = 0;
		sockets[i].socket.pci_irq  = dev->irq;
		sockets[i].socket.cb_dev  = dev;
		sockets[i].socket.owner = THIS_MODULE;

		sockets[i].number = i;
		
		if (card_present(i)) {
			sockets[i].card_state = 3;
			dprintk(KERN_DEBUG "i82092aa: slot %i is occupied\n",i);
		} else {
			dprintk(KERN_DEBUG "i82092aa: slot %i is vacant\n",i);
		}
	}
		
	/* Now, specifiy that all interrupts are to be done as PCI interrupts */
	configbyte = 0xFF; /* bitmask, one bit per event, 1 = PCI interrupt, 0 = ISA interrupt */
	pci_write_config_byte(dev, 0x50, configbyte); /* PCI Interrupt Routing Register */

	/* Register the interrupt handler */
	dprintk(KERN_DEBUG "Requesting interrupt %i \n",dev->irq);
	if ((ret = request_irq(dev->irq, i82092aa_interrupt, IRQF_SHARED, "i82092aa", i82092aa_interrupt))) {
		printk(KERN_ERR "i82092aa: Failed to register IRQ %d, aborting\n", dev->irq);
		goto err_out_free_res;
	}

	pci_set_drvdata(dev, &sockets[i].socket);

	for (i = 0; i<socket_count; i++) {
		sockets[i].socket.dev.parent = &dev->dev;
		sockets[i].socket.ops = &i82092aa_operations;
		sockets[i].socket.resource_ops = &pccard_nonstatic_ops;
		ret = pcmcia_register_socket(&sockets[i].socket);
		if (ret) {
			goto err_out_free_sockets;
		}
	}

	leave("i82092aa_pci_probe");
	return 0;

err_out_free_sockets:
	if (i) {
		for (i--;i>=0;i--) {
			pcmcia_unregister_socket(&sockets[i].socket);
		}
	}
	free_irq(dev->irq, i82092aa_interrupt);
err_out_free_res:
	release_region(pci_resource_start(dev, 0), 2);
err_out_disable:
	pci_disable_device(dev);
	return ret;			
}

static void __devexit i82092aa_pci_remove(struct pci_dev *dev)
{
	struct pcmcia_socket *socket = pci_get_drvdata(dev);

	enter("i82092aa_pci_remove");
	
	free_irq(dev->irq, i82092aa_interrupt);

	if (socket)
		pcmcia_unregister_socket(socket);

	leave("i82092aa_pci_remove");
}

static DEFINE_SPINLOCK(port_lock);

/* basic value read/write functions */

static unsigned char indirect_read(int socket, unsigned short reg)
{
	unsigned short int port;
	unsigned char val;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg += socket * 0x40;
	port = sockets[socket].io_base;
	outb(reg,port);
	val = inb(port+1);
	spin_unlock_irqrestore(&port_lock,flags);
	return val;
}

#if 0
static unsigned short indirect_read16(int socket, unsigned short reg)
{
	unsigned short int port;
	unsigned short tmp;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg  = reg + socket * 0x40;
	port = sockets[socket].io_base;
	outb(reg,port);
	tmp = inb(port+1);
	reg++;
	outb(reg,port);
	tmp = tmp | (inb(port+1)<<8);
	spin_unlock_irqrestore(&port_lock,flags);
	return tmp;
}
#endif

static void indirect_write(int socket, unsigned short reg, unsigned char value)
{
	unsigned short int port;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg = reg + socket * 0x40;
	port = sockets[socket].io_base; 
	outb(reg,port);
	outb(value,port+1);
	spin_unlock_irqrestore(&port_lock,flags);
}

static void indirect_setbit(int socket, unsigned short reg, unsigned char mask)
{
	unsigned short int port;
	unsigned char val;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg = reg + socket * 0x40;
	port = sockets[socket].io_base; 
	outb(reg,port);
	val = inb(port+1);
	val |= mask;
	outb(reg,port);
	outb(val,port+1);
	spin_unlock_irqrestore(&port_lock,flags);
}


static void indirect_resetbit(int socket, unsigned short reg, unsigned char mask)
{
	unsigned short int port;
	unsigned char val;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg = reg + socket * 0x40;
	port = sockets[socket].io_base; 
	outb(reg,port);
	val = inb(port+1);
	val &= ~mask;
	outb(reg,port);
	outb(val,port+1);
	spin_unlock_irqrestore(&port_lock,flags);
}

static void indirect_write16(int socket, unsigned short reg, unsigned short value)
{
	unsigned short int port;
	unsigned char val;
	unsigned long flags;
	spin_lock_irqsave(&port_lock,flags);
	reg = reg + socket * 0x40;
	port = sockets[socket].io_base; 
	
	outb(reg,port);
	val = value & 255;
	outb(val,port+1);
	
	reg++;
	
	outb(reg,port);
	val = value>>8;
	outb(val,port+1);
	spin_unlock_irqrestore(&port_lock,flags);
}

/* simple helper functions */
/* External clock time, in nanoseconds.  120 ns = 8.33 MHz */
static int cycle_time = 120;

static int to_cycles(int ns)
{
	if (cycle_time!=0)
		return ns/cycle_time;
	else
		return 0;
}
    

/* Interrupt handler functionality */

static irqreturn_t i82092aa_interrupt(int irq, void *dev)
{
	int i;
	int loopcount = 0;
	int handled = 0;

	unsigned int events, active=0;
	
/*	enter("i82092aa_interrupt");*/
	
	while (1) {
		loopcount++;
		if (loopcount>20) {
			printk(KERN_ERR "i82092aa: infinite eventloop in interrupt \n");
			break;
		}
		
		active = 0;
		
		for (i=0;i<socket_count;i++) {
			int csc;
			if (sockets[i].card_state==0) /* Inactive socket, should not happen */
				continue;
			
			csc = indirect_read(i,I365_CSC); /* card status change register */
			
			if (csc==0)  /* no events on this socket */
			   	continue;
			handled = 1;
			events = 0;
			 
			if (csc & I365_CSC_DETECT) {
				events |= SS_DETECT;
				printk("Card detected in socket %i!\n",i);
			 }
			
			if (indirect_read(i,I365_INTCTL) & I365_PC_IOCARD) { 
				/* For IO/CARDS, bit 0 means "read the card" */
				events |= (csc & I365_CSC_STSCHG) ? SS_STSCHG : 0; 
			} else {
				/* Check for battery/ready events */
				events |= (csc & I365_CSC_BVD1) ? SS_BATDEAD : 0;
				events |= (csc & I365_CSC_BVD2) ? SS_BATWARN : 0;
				events |= (csc & I365_CSC_READY) ? SS_READY : 0;
			}
			
			if (events) {
				pcmcia_parse_events(&sockets[i].socket, events);
			}
			active |= events;
		}
				
		if (active==0) /* no more events to handle */
			break;				
		
	}
	return IRQ_RETVAL(handled);
/*	leave("i82092aa_interrupt");*/
}



/* socket functions */

static int card_present(int socketno)
{	
	unsigned int val;
	enter("card_present");
	
	if ((socketno<0) || (socketno >= MAX_SOCKETS))
		return 0;
	if (sockets[socketno].io_base == 0)
		return 0;

		
	val = indirect_read(socketno, 1); /* Interface status register */
	if ((val&12)==12) {
		leave("card_present 1");
		return 1;
	}
		
	leave("card_present 0");
	return 0;
}

static void set_bridge_state(int sock)
{
	enter("set_bridge_state");
	indirect_write(sock, I365_GBLCTL,0x00);
	indirect_write(sock, I365_GENCTL,0x00);
	
	indirect_setbit(sock, I365_INTCTL,0x08);
	leave("set_bridge_state");
}





      
static int i82092aa_init(struct pcmcia_socket *sock)
{
	int i;
	struct resource res = { .start = 0, .end = 0x0fff };
        pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { .res = &res, };
        
        enter("i82092aa_init");
                        
        for (i = 0; i < 2; i++) {
        	io.map = i;
                i82092aa_set_io_map(sock, &io);
	}
        for (i = 0; i < 5; i++) {
        	mem.map = i;
                i82092aa_set_mem_map(sock, &mem);
	}
	
	leave("i82092aa_init");
	return 0;
}
                                                                                                                                                                                                                                              
static int i82092aa_get_status(struct pcmcia_socket *socket, u_int *value)
{
	unsigned int sock = container_of(socket, struct socket_info, socket)->number;
	unsigned int status;
	
	enter("i82092aa_get_status");
	
	status = indirect_read(sock,I365_STATUS); /* Interface Status Register */
	*value = 0;
	
	if ((status & I365_CS_DETECT) == I365_CS_DETECT) {
		*value |= SS_DETECT;
	}
		
	/* IO cards have a different meaning of bits 0,1 */
	/* Also notice the inverse-logic on the bits */
	 if (indirect_read(sock, I365_INTCTL) & I365_PC_IOCARD)	{
	 	/* IO card */
	 	if (!(status & I365_CS_STSCHG))
	 		*value |= SS_STSCHG;
	 } else { /* non I/O card */
	 	if (!(status & I365_CS_BVD1))
	 		*value |= SS_BATDEAD;
	 	if (!(status & I365_CS_BVD2))
	 		*value |= SS_BATWARN;
	 		
	 }
	 
	 if (status & I365_CS_WRPROT)
	 	(*value) |= SS_WRPROT;	/* card is write protected */
	 
	 if (status & I365_CS_READY)
	 	(*value) |= SS_READY;    /* card is not busy */
	 	
	 if (status & I365_CS_POWERON)
	 	(*value) |= SS_POWERON;  /* power is applied to the card */


	leave("i82092aa_get_status");
	return 0;
}


static int i82092aa_set_socket(struct pcmcia_socket *socket, socket_state_t *state) 
{
	unsigned int sock = container_of(socket, struct socket_info, socket)->number;
	unsigned char reg;
	
	enter("i82092aa_set_socket");
	
	/* First, set the global controller options */
	
	set_bridge_state(sock);
	
	/* Values for the IGENC register */
	
	reg = 0;
	if (!(state->flags & SS_RESET)) 	/* The reset bit has "inverse" logic */
		reg = reg | I365_PC_RESET;  
	if (state->flags & SS_IOCARD) 
		reg = reg | I365_PC_IOCARD;
		
	indirect_write(sock,I365_INTCTL,reg); /* IGENC, Interrupt and General Control Register */
	
	/* Power registers */
	
	reg = I365_PWR_NORESET; /* default: disable resetdrv on resume */
	
	if (state->flags & SS_PWR_AUTO) {
		printk("Auto power\n");
		reg |= I365_PWR_AUTO;	/* automatic power mngmnt */
	}
	if (state->flags & SS_OUTPUT_ENA) {
		printk("Power Enabled \n");
		reg |= I365_PWR_OUT;	/* enable power */
	}
	
	switch (state->Vcc) {
		case 0:	
			break;
		case 50: 
			printk("setting voltage to Vcc to 5V on socket %i\n",sock);
			reg |= I365_VCC_5V;
			break;
		default:
			printk("i82092aa: i82092aa_set_socket called with invalid VCC power value: %i ", state->Vcc);
			leave("i82092aa_set_socket");
			return -EINVAL;
	}
	
	
	switch (state->Vpp) {
		case 0:	
			printk("not setting Vpp on socket %i\n",sock);
			break;
		case 50: 
			printk("setting Vpp to 5.0 for socket %i\n",sock);
			reg |= I365_VPP1_5V | I365_VPP2_5V;
			break;
		case 120: 
			printk("setting Vpp to 12.0\n");
			reg |= I365_VPP1_12V | I365_VPP2_12V;
			break;
		default:
			printk("i82092aa: i82092aa_set_socket called with invalid VPP power value: %i ", state->Vcc);
			leave("i82092aa_set_socket");
			return -EINVAL;
	}
	
	if (reg != indirect_read(sock,I365_POWER)) /* only write if changed */
		indirect_write(sock,I365_POWER,reg);
		
	/* Enable specific interrupt events */
	
	reg = 0x00;
	if (state->csc_mask & SS_DETECT) {
		reg |= I365_CSC_DETECT;
	}
	if (state->flags & SS_IOCARD) {
		if (state->csc_mask & SS_STSCHG)
			reg |= I365_CSC_STSCHG;
	} else {
		if (state->csc_mask & SS_BATDEAD) 
			reg |= I365_CSC_BVD1;
		if (state->csc_mask & SS_BATWARN) 
			reg |= I365_CSC_BVD2;
		if (state->csc_mask & SS_READY) 
			reg |= I365_CSC_READY; 
		                        
	}
	
	/* now write the value and clear the (probably bogus) pending stuff by doing a dummy read*/
	
	indirect_write(sock,I365_CSCINT,reg);
	(void)indirect_read(sock,I365_CSC);

	leave("i82092aa_set_socket");
	return 0;
}

static int i82092aa_set_io_map(struct pcmcia_socket *socket, struct pccard_io_map *io)
{
	unsigned int sock = container_of(socket, struct socket_info, socket)->number;
	unsigned char map, ioctl;
	
	enter("i82092aa_set_io_map");
	
	map = io->map;
	
	/* Check error conditions */	
	if (map > 1) {
		leave("i82092aa_set_io_map with invalid map");
		return -EINVAL;
	}
	if ((io->start > 0xffff) || (io->stop > 0xffff) || (io->stop < io->start)){
		leave("i82092aa_set_io_map with invalid io");
		return -EINVAL;
	}

	/* Turn off the window before changing anything */ 
	if (indirect_read(sock, I365_ADDRWIN) & I365_ENA_IO(map))
		indirect_resetbit(sock, I365_ADDRWIN, I365_ENA_IO(map));

/*	printk("set_io_map: Setting range to %x - %x \n",io->start,io->stop);  */
	
	/* write the new values */
	indirect_write16(sock,I365_IO(map)+I365_W_START,io->start);            	
	indirect_write16(sock,I365_IO(map)+I365_W_STOP,io->stop);            	
	            	
	ioctl = indirect_read(sock,I365_IOCTL) & ~I365_IOCTL_MASK(map);
	
	if (io->flags & (MAP_16BIT|MAP_AUTOSZ))
		ioctl |= I365_IOCTL_16BIT(map);
		
	indirect_write(sock,I365_IOCTL,ioctl);
	
	/* Turn the window back on if needed */
	if (io->flags & MAP_ACTIVE)
		indirect_setbit(sock,I365_ADDRWIN,I365_ENA_IO(map));
			
	leave("i82092aa_set_io_map");	
	return 0;
}

static int i82092aa_set_mem_map(struct pcmcia_socket *socket, struct pccard_mem_map *mem)
{
	struct socket_info *sock_info = container_of(socket, struct socket_info, socket);
	unsigned int sock = sock_info->number;
	struct pci_bus_region region;
	unsigned short base, i;
	unsigned char map;
	
	enter("i82092aa_set_mem_map");

	pcibios_resource_to_bus(sock_info->dev, &region, mem->res);
	
	map = mem->map;
	if (map > 4) {
		leave("i82092aa_set_mem_map: invalid map");
		return -EINVAL;
	}
	
	
	if ( (mem->card_start > 0x3ffffff) || (region.start > region.end) ||
	     (mem->speed > 1000) ) {
		leave("i82092aa_set_mem_map: invalid address / speed");
		printk("invalid mem map for socket %i: %llx to %llx with a "
			"start of %x\n",
			sock,
			(unsigned long long)region.start,
			(unsigned long long)region.end,
			mem->card_start);
		return -EINVAL;
	}
	
	/* Turn off the window before changing anything */
	if (indirect_read(sock, I365_ADDRWIN) & I365_ENA_MEM(map))
	              indirect_resetbit(sock, I365_ADDRWIN, I365_ENA_MEM(map));
	                 
	                 
/* 	printk("set_mem_map: Setting map %i range to %x - %x on socket %i, speed is %i, active = %i \n",map, region.start,region.end,sock,mem->speed,mem->flags & MAP_ACTIVE);  */

	/* write the start address */
	base = I365_MEM(map);
	i = (region.start >> 12) & 0x0fff;
	if (mem->flags & MAP_16BIT) 
		i |= I365_MEM_16BIT;
	if (mem->flags & MAP_0WS)
		i |= I365_MEM_0WS;	
	indirect_write16(sock,base+I365_W_START,i);
		               
	/* write the stop address */
	
	i= (region.end >> 12) & 0x0fff;
	switch (to_cycles(mem->speed)) {
		case 0:
			break;
		case 1:
			i |= I365_MEM_WS0;
			break;
		case 2:
			i |= I365_MEM_WS1;
			break;
		default:
			i |= I365_MEM_WS1 | I365_MEM_WS0;
			break;
	}
	
	indirect_write16(sock,base+I365_W_STOP,i);
	
	/* card start */
	
	i = ((mem->card_start - region.start) >> 12) & 0x3fff;
	if (mem->flags & MAP_WRPROT)
		i |= I365_MEM_WRPROT;
	if (mem->flags & MAP_ATTRIB) {
/*		printk("requesting attribute memory for socket %i\n",sock);*/
		i |= I365_MEM_REG;
	} else {
/*		printk("requesting normal memory for socket %i\n",sock);*/
	}
	indirect_write16(sock,base+I365_W_OFF,i);
	
	/* Enable the window if necessary */
	if (mem->flags & MAP_ACTIVE)
		indirect_setbit(sock, I365_ADDRWIN, I365_ENA_MEM(map));
	            
	leave("i82092aa_set_mem_map");
	return 0;
}

static int i82092aa_module_init(void)
{
	return pci_register_driver(&i82092aa_pci_driver);
}

static void i82092aa_module_exit(void)
{
	enter("i82092aa_module_exit");
	pci_unregister_driver(&i82092aa_pci_driver);
	if (sockets[0].io_base>0)
			 release_region(sockets[0].io_base, 2);
	leave("i82092aa_module_exit");
}

module_init(i82092aa_module_init);
module_exit(i82092aa_module_exit);

