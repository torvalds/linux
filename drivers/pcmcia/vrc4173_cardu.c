/*
 * FILE NAME
 *	drivers/pcmcia/vrc4173_cardu.c
 *
 * BRIEF MODULE DESCRIPTION
 * 	NEC VRC4173 CARDU driver for Socket Services
 *	(This device doesn't support CardBus. it is supporting only 16bit PC Card.)
 *
 * Copyright 2002,2003 Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/io.h>

#include <pcmcia/ss.h>

#include "vrc4173_cardu.h"

MODULE_DESCRIPTION("NEC VRC4173 CARDU driver for Socket Services");
MODULE_AUTHOR("Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>");
MODULE_LICENSE("GPL");

static int vrc4173_cardu_slots;

static vrc4173_socket_t cardu_sockets[CARDU_MAX_SOCKETS];

extern struct socket_info_t *pcmcia_register_socket (int slot,
                                                     struct pccard_operations *vtable,
                                                     int use_bus_pm);
extern void pcmcia_unregister_socket(struct socket_info_t *s);

static inline uint8_t exca_readb(vrc4173_socket_t *socket, uint16_t offset)
{
	return readb(socket->base + EXCA_REGS_BASE + offset);
}

static inline uint16_t exca_readw(vrc4173_socket_t *socket, uint16_t offset)
{
	uint16_t val;

	val = readb(socket->base + EXCA_REGS_BASE + offset);
	val |= (u16)readb(socket->base + EXCA_REGS_BASE + offset + 1) << 8;

	return val;
}

static inline void exca_writeb(vrc4173_socket_t *socket, uint16_t offset, uint8_t val)
{
	writeb(val, socket->base + EXCA_REGS_BASE + offset);
}

static inline void exca_writew(vrc4173_socket_t *socket, uint8_t offset, uint16_t val)
{
	writeb((u8)val, socket->base + EXCA_REGS_BASE + offset);
	writeb((u8)(val >> 8), socket->base + EXCA_REGS_BASE + offset + 1);
}

static inline uint32_t cardbus_socket_readl(vrc4173_socket_t *socket, u16 offset)
{
	return readl(socket->base + CARDBUS_SOCKET_REGS_BASE + offset);
}

static inline void cardbus_socket_writel(vrc4173_socket_t *socket, u16 offset, uint32_t val)
{
	writel(val, socket->base + CARDBUS_SOCKET_REGS_BASE + offset);
}

static void cardu_pciregs_init(struct pci_dev *dev)
{
	u32 syscnt;
	u16 brgcnt;
	u8 devcnt;

	pci_write_config_dword(dev, 0x1c, 0x10000000);
	pci_write_config_dword(dev, 0x20, 0x17fff000);
	pci_write_config_dword(dev, 0x2c, 0);
	pci_write_config_dword(dev, 0x30, 0xfffc);

	pci_read_config_word(dev, BRGCNT, &brgcnt);
	brgcnt &= ~IREQ_INT;
	pci_write_config_word(dev, BRGCNT, brgcnt);

	pci_read_config_dword(dev, SYSCNT, &syscnt);
	syscnt &= ~(BAD_VCC_REQ_DISB|PCPCI_EN|CH_ASSIGN_MASK|SUB_ID_WR_EN|PCI_CLK_RIN);
	syscnt |= (CH_ASSIGN_NODMA|ASYN_INT_MODE);
	pci_write_config_dword(dev, SYSCNT, syscnt);

	pci_read_config_byte(dev, DEVCNT, &devcnt);
	devcnt &= ~(ZOOM_VIDEO_EN|SR_PCI_INT_SEL_MASK|PCI_INT_MODE|IRQ_MODE);
	devcnt |= (SR_PCI_INT_SEL_NONE|IFG);
	pci_write_config_byte(dev, DEVCNT, devcnt);

	pci_write_config_byte(dev, CHIPCNT, S_PREF_DISB);

	pci_write_config_byte(dev, SERRDIS, 0);
}

static int cardu_init(unsigned int slot)
{
	vrc4173_socket_t *socket = &cardu_sockets[slot];

	cardu_pciregs_init(socket->dev);

	/* CARD_SC bits are cleared by reading CARD_SC. */
	exca_writeb(socket, GLO_CNT, 0);

	socket->cap.features |= SS_CAP_PCCARD | SS_CAP_PAGE_REGS;
	socket->cap.irq_mask = 0;
	socket->cap.map_size = 0x1000;
	socket->cap.pci_irq  = socket->dev->irq;
	socket->events = 0;
	spin_lock_init(socket->event_lock);

	/* Enable PC Card status interrupts */
	exca_writeb(socket, CARD_SCI, CARD_DT_EN|RDY_EN|BAT_WAR_EN|BAT_DEAD_EN);

	return 0;
}

static int cardu_register_callback(unsigned int sock,
                                           void (*handler)(void *, unsigned int),
                                           void * info)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];

	socket->handler = handler;
	socket->info = info;

	return 0;
}

static int cardu_inquire_socket(unsigned int sock, socket_cap_t *cap)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];

	*cap = socket->cap;

	return 0;
}

static int cardu_get_status(unsigned int sock, u_int *value)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint32_t state;
	uint8_t status;
	u_int val = 0;

	status = exca_readb(socket, IF_STATUS);
	if (status & CARD_PWR) val |= SS_POWERON;
	if (status & READY) val |= SS_READY;
	if (status & CARD_WP) val |= SS_WRPROT;
	if ((status & (CARD_DETECT1|CARD_DETECT2)) == (CARD_DETECT1|CARD_DETECT2))
		val |= SS_DETECT;
	if (exca_readb(socket, INT_GEN_CNT) & CARD_TYPE_IO) {
		if (status & STSCHG) val |= SS_STSCHG;
	} else {
		status &= BV_DETECT_MASK;
		if (status != BV_DETECT_GOOD) {
			if (status == BV_DETECT_WARN) val |= SS_BATWARN;
			else val |= SS_BATDEAD;
		}
	}

	state = cardbus_socket_readl(socket, SKT_PRE_STATE);
	if (state & VOL_3V_CARD_DT) val |= SS_3VCARD;
	if (state & VOL_XV_CARD_DT) val |= SS_XVCARD;
	if (state & CB_CARD_DT) val |= SS_CARDBUS;
	if (!(state &
	      (VOL_YV_CARD_DT|VOL_XV_CARD_DT|VOL_3V_CARD_DT|VOL_5V_CARD_DT|CCD20|CCD10)))
		val |= SS_PENDING;

	*value = val;

	return 0;
}

static inline uint8_t set_Vcc_value(u_char Vcc)
{
	switch (Vcc) {
	case 33:
		return VCC_3V;
	case 50:
		return VCC_5V;
	}

	return VCC_0V;
}

static inline uint8_t set_Vpp_value(u_char Vpp)
{
	switch (Vpp) {
	case 33:
	case 50:
		return VPP_VCC;
	case 120:
		return VPP_12V;
	}

	return VPP_0V;
}

static int cardu_set_socket(unsigned int sock, socket_state_t *state)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint8_t val;

	if (((state->Vpp == 33) || (state->Vpp == 50)) && (state->Vpp != state->Vcc))
			return -EINVAL;

	val = set_Vcc_value(state->Vcc);
	val |= set_Vpp_value(state->Vpp);
	if (state->flags & SS_OUTPUT_ENA) val |= CARD_OUT_EN;
	exca_writeb(socket, PWR_CNT, val);

	val = exca_readb(socket, INT_GEN_CNT) & CARD_REST0;
	if (state->flags & SS_RESET) val &= ~CARD_REST0;
	else val |= CARD_REST0;
	if (state->flags & SS_IOCARD) val |= CARD_TYPE_IO;
	exca_writeb(socket, INT_GEN_CNT, val);

	return 0;
}

static int cardu_get_io_map(unsigned int sock, struct pccard_io_map *io)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint8_t ioctl, window;
	u_char map;

	map = io->map;
	if (map > 1)
		return -EINVAL;

	io->start = exca_readw(socket, IO_WIN_SA(map));
	io->stop = exca_readw(socket, IO_WIN_EA(map));

	ioctl = exca_readb(socket, IO_WIN_CNT);
	window = exca_readb(socket, ADR_WIN_EN);
	io->flags  = (window & IO_WIN_EN(map)) ? MAP_ACTIVE : 0;
	if (ioctl & IO_WIN_DATA_AUTOSZ(map))
		io->flags |= MAP_AUTOSZ;
	else if (ioctl & IO_WIN_DATA_16BIT(map))
		io->flags |= MAP_16BIT;

	return 0;
}

static int cardu_set_io_map(unsigned int sock, struct pccard_io_map *io)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint16_t ioctl;
	uint8_t window, enable;
	u_char map;

	map = io->map;
	if (map > 1)
		return -EINVAL;

	window = exca_readb(socket, ADR_WIN_EN);
	enable = IO_WIN_EN(map);

	if (window & enable) {
		window &= ~enable;
		exca_writeb(socket, ADR_WIN_EN, window);
	}

	exca_writew(socket, IO_WIN_SA(map), io->start);
	exca_writew(socket, IO_WIN_EA(map), io->stop);

	ioctl = exca_readb(socket, IO_WIN_CNT) & ~IO_WIN_CNT_MASK(map);
	if (io->flags & MAP_AUTOSZ) ioctl |= IO_WIN_DATA_AUTOSZ(map);
	else if (io->flags & MAP_16BIT) ioctl |= IO_WIN_DATA_16BIT(map);
	exca_writeb(socket, IO_WIN_CNT, ioctl);

	if (io->flags & MAP_ACTIVE)
		exca_writeb(socket, ADR_WIN_EN, window | enable);

	return 0;
}

static int cardu_get_mem_map(unsigned int sock, struct pccard_mem_map *mem)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint32_t start, stop, offset, page;
	uint8_t window;
	u_char map;

	map = mem->map;
	if (map > 4)
		return -EINVAL;

	window = exca_readb(socket, ADR_WIN_EN);
	mem->flags = (window & MEM_WIN_EN(map)) ? MAP_ACTIVE : 0;

	start = exca_readw(socket, MEM_WIN_SA(map));
	mem->flags |= (start & MEM_WIN_DSIZE) ? MAP_16BIT : 0;
	start = (start & 0x0fff) << 12;

	stop = exca_readw(socket, MEM_WIN_EA(map));
	stop = ((stop & 0x0fff) << 12) + 0x0fff;

	offset = exca_readw(socket, MEM_WIN_OA(map));
	mem->flags |= (offset & MEM_WIN_WP) ? MAP_WRPROT : 0;
	mem->flags |= (offset & MEM_WIN_REGSET) ? MAP_ATTRIB : 0;
	offset = ((offset & 0x3fff) << 12) + start;
	mem->card_start = offset & 0x03ffffff;

	page = exca_readb(socket, MEM_WIN_SAU(map)) << 24;
	mem->sys_start = start + page;
	mem->sys_stop = start + page;

	return 0;
}

static int cardu_set_mem_map(unsigned int sock, struct pccard_mem_map *mem)
{
	vrc4173_socket_t *socket = &cardu_sockets[sock];
	uint16_t value;
	uint8_t window, enable;
	u_long sys_start, sys_stop, card_start;
	u_char map;

	map = mem->map;
	sys_start = mem->sys_start;
	sys_stop = mem->sys_stop;
	card_start = mem->card_start;

	if (map > 4 || sys_start > sys_stop || ((sys_start ^ sys_stop) >> 24) ||
	    (card_start >> 26))
		return -EINVAL;

	window = exca_readb(socket, ADR_WIN_EN);
	enable = MEM_WIN_EN(map);
	if (window & enable) {
		window &= ~enable;
		exca_writeb(socket, ADR_WIN_EN, window);
	}

	exca_writeb(socket, MEM_WIN_SAU(map), sys_start >> 24);

	value = (sys_start >> 12) & 0x0fff;
	if (mem->flags & MAP_16BIT) value |= MEM_WIN_DSIZE;
	exca_writew(socket, MEM_WIN_SA(map), value);

	value = (sys_stop >> 12) & 0x0fff;
	exca_writew(socket, MEM_WIN_EA(map), value);

	value = ((card_start - sys_start) >> 12) & 0x3fff;
	if (mem->flags & MAP_WRPROT) value |= MEM_WIN_WP;
	if (mem->flags & MAP_ATTRIB) value |= MEM_WIN_REGSET;
	exca_writew(socket, MEM_WIN_OA(map), value);

	if (mem->flags & MAP_ACTIVE)
		exca_writeb(socket, ADR_WIN_EN, window | enable);

	return 0;
}

static void cardu_proc_setup(unsigned int sock, struct proc_dir_entry *base)
{
}

static struct pccard_operations cardu_operations = {
	.init			= cardu_init,
	.register_callback	= cardu_register_callback,
	.inquire_socket		= cardu_inquire_socket,
	.get_status		= cardu_get_status,
	.set_socket		= cardu_set_socket,
	.get_io_map		= cardu_get_io_map,
	.set_io_map		= cardu_set_io_map,
	.get_mem_map		= cardu_get_mem_map,
	.set_mem_map		= cardu_set_mem_map,
	.proc_setup		= cardu_proc_setup,
};

static void cardu_bh(void *data)
{
	vrc4173_socket_t *socket = (vrc4173_socket_t *)data;
	uint16_t events;

	spin_lock_irq(&socket->event_lock);
	events = socket->events;
	socket->events = 0;
	spin_unlock_irq(&socket->event_lock);

	if (socket->handler)
		socket->handler(socket->info, events);
}

static uint16_t get_events(vrc4173_socket_t *socket)
{
	uint16_t events = 0;
	uint8_t csc, status;

	status = exca_readb(socket, IF_STATUS);
	csc = exca_readb(socket, CARD_SC);
	if ((csc & CARD_DT_CHG) &&
	    ((status & (CARD_DETECT1|CARD_DETECT2)) == (CARD_DETECT1|CARD_DETECT2)))
		events |= SS_DETECT;

	if ((csc & RDY_CHG) && (status & READY))
		events |= SS_READY;

	if (exca_readb(socket, INT_GEN_CNT) & CARD_TYPE_IO) {
		if ((csc & BAT_DEAD_ST_CHG) && (status & STSCHG))
			events |= SS_STSCHG;
	} else {
		if (csc & (BAT_WAR_CHG|BAT_DEAD_ST_CHG)) {
			if ((status & BV_DETECT_MASK) != BV_DETECT_GOOD) {
				if (status == BV_DETECT_WARN) events |= SS_BATWARN;
				else events |= SS_BATDEAD;
			}
		}
	}

	return events;
}

static void cardu_interrupt(int irq, void *dev_id)
{
	vrc4173_socket_t *socket = (vrc4173_socket_t *)dev_id;
	uint16_t events;

	INIT_WORK(&socket->tq_work, cardu_bh, socket);

	events = get_events(socket);
	if (events) {
		spin_lock(&socket->event_lock);
		socket->events |= events;
		spin_unlock(&socket->event_lock);
		schedule_work(&socket->tq_work);
	}
}

static int __devinit vrc4173_cardu_probe(struct pci_dev *dev,
                                         const struct pci_device_id *ent)
{
	vrc4173_socket_t *socket;
	unsigned long start, len, flags;
	int slot, err;

	slot = vrc4173_cardu_slots++;
	socket = &cardu_sockets[slot];
	if (socket->noprobe != 0)
		return -EBUSY;

	sprintf(socket->name, "NEC VRC4173 CARDU%1d", slot+1);

	if ((err = pci_enable_device(dev)) < 0)
		return err;

	start = pci_resource_start(dev, 0);
	if (start == 0)
		return -ENODEV;

	len = pci_resource_len(dev, 0);
	if (len == 0)
		return -ENODEV;

	if (((flags = pci_resource_flags(dev, 0)) & IORESOURCE_MEM) == 0)
		return -EBUSY;

	if ((err = pci_request_regions(dev, socket->name)) < 0)
		return err;

	socket->base = ioremap(start, len);
	if (socket->base == NULL)
		return -ENODEV;

	socket->dev = dev;

	socket->pcmcia_socket = pcmcia_register_socket(slot, &cardu_operations, 1);
	if (socket->pcmcia_socket == NULL) {
		iounmap(socket->base);
		socket->base = NULL;
		return -ENOMEM;
	}

	if (request_irq(dev->irq, cardu_interrupt, IRQF_SHARED, socket->name, socket) < 0) {
		pcmcia_unregister_socket(socket->pcmcia_socket);
		socket->pcmcia_socket = NULL;
		iounmap(socket->base);
		socket->base = NULL;
		return -EBUSY;
	}

	printk(KERN_INFO "%s at %#08lx, IRQ %d\n", socket->name, start, dev->irq);

	return 0;
}

static int __devinit vrc4173_cardu_setup(char *options)
{
	if (options == NULL || *options == '\0')
		return 1;

	if (strncmp(options, "cardu1:", 7) == 0) {
		options += 7;
		if (*options != '\0') {
			if (strncmp(options, "noprobe", 7) == 0) {
				cardu_sockets[CARDU1].noprobe = 1;
				options += 7;
			}

			if (*options != ',')
				return 1;
		} else
			return 1;
	}

	if (strncmp(options, "cardu2:", 7) == 0) {
		options += 7;
		if ((*options != '\0') && (strncmp(options, "noprobe", 7) == 0))
			cardu_sockets[CARDU2].noprobe = 1;
	}

	return 1;
}

__setup("vrc4173_cardu=", vrc4173_cardu_setup);

static struct pci_device_id vrc4173_cardu_id_table[] __devinitdata = {
	{	.vendor		= PCI_VENDOR_ID_NEC,
		.device		= PCI_DEVICE_ID_NEC_NAPCCARD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID, },
        {0, }
};

static struct pci_driver vrc4173_cardu_driver = {
	.name		= "NEC VRC4173 CARDU",
	.probe		= vrc4173_cardu_probe,
	.id_table	= vrc4173_cardu_id_table,
};

static int __devinit vrc4173_cardu_init(void)
{
	vrc4173_cardu_slots = 0;

	return pci_register_driver(&vrc4173_cardu_driver);
}

static void __devexit vrc4173_cardu_exit(void)
{
	pci_unregister_driver(&vrc4173_cardu_driver);
}

module_init(vrc4173_cardu_init);
module_exit(vrc4173_cardu_exit);
MODULE_DEVICE_TABLE(pci, vrc4173_cardu_id_table);
