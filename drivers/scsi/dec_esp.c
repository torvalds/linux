/*
 * dec_esp.c: Driver for SCSI chips on IOASIC based TURBOchannel DECstations
 *            and TURBOchannel PMAZ-A cards
 *
 * TURBOchannel changes by Harald Koerfgen
 * PMAZ-A support by David Airlie
 *
 * based on jazz_esp.c:
 * Copyright (C) 1997 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * jazz_esp is based on David S. Miller's ESP driver and cyber_esp
 *
 * 20000819 - Small PMAZ-AA fixes by Florian Lohoff <flo@rfc822.org>
 *            Be warned the PMAZ-AA works currently as a single card.
 *            Dont try to put multiple cards in one machine - They are
 *            both detected but it may crash under high load garbling your
 *            data.
 * 20001005	- Initialization fixes for 2.4.0-test9
 * 			  Florian Lohoff <flo@rfc822.org>
 *
 *	Copyright (C) 2002, 2003, 2005  Maciej W. Rozycki
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/stat.h>

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/ioasic_ints.h>
#include <asm/dec/machtype.h>
#include <asm/dec/system.h>
#include <asm/dec/tc.h>

#define DEC_SCSI_SREG 0
#define DEC_SCSI_DMAREG 0x40000
#define DEC_SCSI_SRAM 0x80000
#define DEC_SCSI_DIAG 0xC0000

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "NCR53C9x.h"

static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static void dma_drain(struct NCR_ESP *esp);
static int  dma_can_transfer(struct NCR_ESP *esp, struct scsi_cmnd *sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, u32 vaddress, int length);
static void dma_init_write(struct NCR_ESP *esp, u32 vaddress, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, u32 addr, int count, int write);
static void dma_mmu_get_scsi_one(struct NCR_ESP *esp, struct scsi_cmnd * sp);
static void dma_mmu_get_scsi_sgl(struct NCR_ESP *esp, struct scsi_cmnd * sp);
static void dma_advance_sg(struct scsi_cmnd * sp);

static void pmaz_dma_drain(struct NCR_ESP *esp);
static void pmaz_dma_init_read(struct NCR_ESP *esp, u32 vaddress, int length);
static void pmaz_dma_init_write(struct NCR_ESP *esp, u32 vaddress, int length);
static void pmaz_dma_ints_off(struct NCR_ESP *esp);
static void pmaz_dma_ints_on(struct NCR_ESP *esp);
static void pmaz_dma_setup(struct NCR_ESP *esp, u32 addr, int count, int write);
static void pmaz_dma_mmu_get_scsi_one(struct NCR_ESP *esp, struct scsi_cmnd * sp);

#define TC_ESP_RAM_SIZE 0x20000
#define ESP_TGT_DMA_SIZE ((TC_ESP_RAM_SIZE/7) & ~(sizeof(int)-1))
#define ESP_NCMD 7

#define TC_ESP_DMAR_MASK  0x1ffff
#define TC_ESP_DMAR_WRITE 0x80000000
#define TC_ESP_DMA_ADDR(x) ((unsigned)(x) & TC_ESP_DMAR_MASK)

u32 esp_virt_buffer;
int scsi_current_length;

volatile unsigned char cmd_buffer[16];
volatile unsigned char pmaz_cmd_buffer[16];
				/* This is where all commands are put
				 * before they are trasfered to the ESP chip
				 * via PIO.
				 */

static irqreturn_t scsi_dma_merr_int(int, void *, struct pt_regs *);
static irqreturn_t scsi_dma_err_int(int, void *, struct pt_regs *);
static irqreturn_t scsi_dma_int(int, void *, struct pt_regs *);

static int dec_esp_detect(struct scsi_host_template * tpnt);

static int dec_esp_release(struct Scsi_Host *shost)
{
	if (shost->irq)
		free_irq(shost->irq, NULL);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_unregister(shost);
	return 0;
}

static struct scsi_host_template driver_template = {
	.proc_name		= "dec_esp",
	.proc_info		= esp_proc_info,
	.name			= "NCR53C94",
	.detect			= dec_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= dec_esp_release,
	.info			= esp_info,
	.queuecommand		= esp_queue,
	.eh_abort_handler	= esp_abort,
	.eh_bus_reset_handler	= esp_reset,
	.can_queue		= 7,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= DISABLE_CLUSTERING,
};


#include "scsi_module.c"

/***************************************************************** Detection */
static int dec_esp_detect(Scsi_Host_Template * tpnt)
{
	struct NCR_ESP *esp;
	struct ConfigDev *esp_dev;
	int slot;
	unsigned long mem_start;

	if (IOASIC) {
		esp_dev = 0;
		esp = esp_allocate(tpnt, (void *) esp_dev);

		/* Do command transfer with programmed I/O */
		esp->do_pio_cmds = 1;

		/* Required functions */
		esp->dma_bytes_sent = &dma_bytes_sent;
		esp->dma_can_transfer = &dma_can_transfer;
		esp->dma_dump_state = &dma_dump_state;
		esp->dma_init_read = &dma_init_read;
		esp->dma_init_write = &dma_init_write;
		esp->dma_ints_off = &dma_ints_off;
		esp->dma_ints_on = &dma_ints_on;
		esp->dma_irq_p = &dma_irq_p;
		esp->dma_ports_p = &dma_ports_p;
		esp->dma_setup = &dma_setup;

		/* Optional functions */
		esp->dma_barrier = 0;
		esp->dma_drain = &dma_drain;
		esp->dma_invalidate = 0;
		esp->dma_irq_entry = 0;
		esp->dma_irq_exit = 0;
		esp->dma_poll = 0;
		esp->dma_reset = 0;
		esp->dma_led_off = 0;
		esp->dma_led_on = 0;

		/* virtual DMA functions */
		esp->dma_mmu_get_scsi_one = &dma_mmu_get_scsi_one;
		esp->dma_mmu_get_scsi_sgl = &dma_mmu_get_scsi_sgl;
		esp->dma_mmu_release_scsi_one = 0;
		esp->dma_mmu_release_scsi_sgl = 0;
		esp->dma_advance_sg = &dma_advance_sg;


		/* SCSI chip speed */
		esp->cfreq = 25000000;

		esp->dregs = 0;

		/* ESP register base */
		esp->eregs = (void *)CKSEG1ADDR(dec_kn_slot_base +
						IOASIC_SCSI);

		/* Set the command buffer */
		esp->esp_command = (volatile unsigned char *) cmd_buffer;

		/* get virtual dma address for command buffer */
		esp->esp_command_dvma = virt_to_phys(cmd_buffer);

		esp->irq = dec_interrupt[DEC_IRQ_ASC];

		esp->scsi_id = 7;

		/* Check for differential SCSI-bus */
		esp->diff = 0;

		esp_initialize(esp);

		if (request_irq(esp->irq, esp_intr, SA_INTERRUPT,
				"ncr53c94", esp->ehost))
			goto err_dealloc;
		if (request_irq(dec_interrupt[DEC_IRQ_ASC_MERR],
				scsi_dma_merr_int, SA_INTERRUPT,
				"ncr53c94 error", esp->ehost))
			goto err_free_irq;
		if (request_irq(dec_interrupt[DEC_IRQ_ASC_ERR],
				scsi_dma_err_int, SA_INTERRUPT,
				"ncr53c94 overrun", esp->ehost))
			goto err_free_irq_merr;
		if (request_irq(dec_interrupt[DEC_IRQ_ASC_DMA],
				scsi_dma_int, SA_INTERRUPT,
				"ncr53c94 dma", esp->ehost))
			goto err_free_irq_err;

	}

	if (TURBOCHANNEL) {
		while ((slot = search_tc_card("PMAZ-AA")) >= 0) {
			claim_tc_card(slot);

			esp_dev = 0;
			esp = esp_allocate(tpnt, (void *) esp_dev);

			mem_start = get_tc_base_addr(slot);

			/* Store base addr into esp struct */
			esp->slot = CPHYSADDR(mem_start);

			esp->dregs = 0;
			esp->eregs = (void *)CKSEG1ADDR(mem_start +
							DEC_SCSI_SREG);
			esp->do_pio_cmds = 1;

			/* Set the command buffer */
			esp->esp_command = (volatile unsigned char *) pmaz_cmd_buffer;

			/* get virtual dma address for command buffer */
			esp->esp_command_dvma = virt_to_phys(pmaz_cmd_buffer);

			esp->cfreq = get_tc_speed();

			esp->irq = get_tc_irq_nr(slot);

			/* Required functions */
			esp->dma_bytes_sent = &dma_bytes_sent;
			esp->dma_can_transfer = &dma_can_transfer;
			esp->dma_dump_state = &dma_dump_state;
			esp->dma_init_read = &pmaz_dma_init_read;
			esp->dma_init_write = &pmaz_dma_init_write;
			esp->dma_ints_off = &pmaz_dma_ints_off;
			esp->dma_ints_on = &pmaz_dma_ints_on;
			esp->dma_irq_p = &dma_irq_p;
			esp->dma_ports_p = &dma_ports_p;
			esp->dma_setup = &pmaz_dma_setup;

			/* Optional functions */
			esp->dma_barrier = 0;
			esp->dma_drain = &pmaz_dma_drain;
			esp->dma_invalidate = 0;
			esp->dma_irq_entry = 0;
			esp->dma_irq_exit = 0;
			esp->dma_poll = 0;
			esp->dma_reset = 0;
			esp->dma_led_off = 0;
			esp->dma_led_on = 0;

			esp->dma_mmu_get_scsi_one = pmaz_dma_mmu_get_scsi_one;
			esp->dma_mmu_get_scsi_sgl = 0;
			esp->dma_mmu_release_scsi_one = 0;
			esp->dma_mmu_release_scsi_sgl = 0;
			esp->dma_advance_sg = 0;

 			if (request_irq(esp->irq, esp_intr, SA_INTERRUPT,
 					 "PMAZ_AA", esp->ehost)) {
 				esp_deallocate(esp);
 				release_tc_card(slot);
 				continue;
 			}
			esp->scsi_id = 7;
			esp->diff = 0;
			esp_initialize(esp);
		}
	}

	if(nesps) {
		printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps, esps_in_use);
		esps_running = esps_in_use;
		return esps_in_use;
	}
	return 0;

err_free_irq_err:
	free_irq(dec_interrupt[DEC_IRQ_ASC_ERR], scsi_dma_err_int);
err_free_irq_merr:
	free_irq(dec_interrupt[DEC_IRQ_ASC_MERR], scsi_dma_merr_int);
err_free_irq:
	free_irq(esp->irq, esp_intr);
err_dealloc:
	esp_deallocate(esp);
	return 0;
}

/************************************************************* DMA Functions */
static irqreturn_t scsi_dma_merr_int(int irq, void *dev_id, struct pt_regs *regs)
{
	printk("Got unexpected SCSI DMA Interrupt! < ");
	printk("SCSI_DMA_MEMRDERR ");
	printk(">\n");

	return IRQ_HANDLED;
}

static irqreturn_t scsi_dma_err_int(int irq, void *dev_id, struct pt_regs *regs)
{
	/* empty */

	return IRQ_HANDLED;
}

static irqreturn_t scsi_dma_int(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 scsi_next_ptr;

	scsi_next_ptr = ioasic_read(IO_REG_SCSI_DMA_P);

	/* next page */
	scsi_next_ptr = (((scsi_next_ptr >> 3) + PAGE_SIZE) & PAGE_MASK) << 3;
	ioasic_write(IO_REG_SCSI_DMA_BP, scsi_next_ptr);
	fast_iob();

	return IRQ_HANDLED;
}

static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
	return fifo_count;
}

static void dma_drain(struct NCR_ESP *esp)
{
	u32 nw, data0, data1, scsi_data_ptr;
	u16 *p;

	nw = ioasic_read(IO_REG_SCSI_SCR);

	/*
	 * Is there something in the dma buffers left?
	 */
	if (nw) {
		scsi_data_ptr = ioasic_read(IO_REG_SCSI_DMA_P) >> 3;
		p = phys_to_virt(scsi_data_ptr);
		switch (nw) {
		case 1:
			data0 = ioasic_read(IO_REG_SCSI_SDR0);
			p[0] = data0 & 0xffff;
			break;
		case 2:
			data0 = ioasic_read(IO_REG_SCSI_SDR0);
			p[0] = data0 & 0xffff;
			p[1] = (data0 >> 16) & 0xffff;
			break;
		case 3:
			data0 = ioasic_read(IO_REG_SCSI_SDR0);
			data1 = ioasic_read(IO_REG_SCSI_SDR1);
			p[0] = data0 & 0xffff;
			p[1] = (data0 >> 16) & 0xffff;
			p[2] = data1 & 0xffff;
			break;
		default:
			printk("Strange: %d words in dma buffer left\n", nw);
			break;
		}
	}
}

static int dma_can_transfer(struct NCR_ESP *esp, struct scsi_cmnd * sp)
{
	return sp->SCp.this_residual;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
}

static void dma_init_read(struct NCR_ESP *esp, u32 vaddress, int length)
{
	u32 scsi_next_ptr, ioasic_ssr;
	unsigned long flags;

	if (vaddress & 3)
		panic("dec_esp.c: unable to handle partial word transfers, yet...");

	dma_cache_wback_inv((unsigned long) phys_to_virt(vaddress), length);

	spin_lock_irqsave(&ioasic_ssr_lock, flags);

	fast_mb();
	ioasic_ssr = ioasic_read(IO_REG_SSR);

	ioasic_ssr &= ~IO_SSR_SCSI_DMA_EN;
	ioasic_write(IO_REG_SSR, ioasic_ssr);

	fast_wmb();
	ioasic_write(IO_REG_SCSI_SCR, 0);
	ioasic_write(IO_REG_SCSI_DMA_P, vaddress << 3);

	/* prepare for next page */
	scsi_next_ptr = ((vaddress + PAGE_SIZE) & PAGE_MASK) << 3;
	ioasic_write(IO_REG_SCSI_DMA_BP, scsi_next_ptr);

	ioasic_ssr |= (IO_SSR_SCSI_DMA_DIR | IO_SSR_SCSI_DMA_EN);
	fast_wmb();
	ioasic_write(IO_REG_SSR, ioasic_ssr);

	fast_iob();
	spin_unlock_irqrestore(&ioasic_ssr_lock, flags);
}

static void dma_init_write(struct NCR_ESP *esp, u32 vaddress, int length)
{
	u32 scsi_next_ptr, ioasic_ssr;
	unsigned long flags;

	if (vaddress & 3)
		panic("dec_esp.c: unable to handle partial word transfers, yet...");

	dma_cache_wback_inv((unsigned long) phys_to_virt(vaddress), length);

	spin_lock_irqsave(&ioasic_ssr_lock, flags);

	fast_mb();
	ioasic_ssr = ioasic_read(IO_REG_SSR);

	ioasic_ssr &= ~(IO_SSR_SCSI_DMA_DIR | IO_SSR_SCSI_DMA_EN);
	ioasic_write(IO_REG_SSR, ioasic_ssr);

	fast_wmb();
	ioasic_write(IO_REG_SCSI_SCR, 0);
	ioasic_write(IO_REG_SCSI_DMA_P, vaddress << 3);

	/* prepare for next page */
	scsi_next_ptr = ((vaddress + PAGE_SIZE) & PAGE_MASK) << 3;
	ioasic_write(IO_REG_SCSI_DMA_BP, scsi_next_ptr);

	ioasic_ssr |= IO_SSR_SCSI_DMA_EN;
	fast_wmb();
	ioasic_write(IO_REG_SSR, ioasic_ssr);

	fast_iob();
	spin_unlock_irqrestore(&ioasic_ssr_lock, flags);
}

static void dma_ints_off(struct NCR_ESP *esp)
{
	disable_irq(dec_interrupt[DEC_IRQ_ASC_DMA]);
}

static void dma_ints_on(struct NCR_ESP *esp)
{
	enable_irq(dec_interrupt[DEC_IRQ_ASC_DMA]);
}

static int dma_irq_p(struct NCR_ESP *esp)
{
	return (esp->eregs->esp_status & ESP_STAT_INTR);
}

static int dma_ports_p(struct NCR_ESP *esp)
{
	/*
	 * FIXME: what's this good for?
	 */
	return 1;
}

static void dma_setup(struct NCR_ESP *esp, u32 addr, int count, int write)
{
	/*
	 * DMA_ST_WRITE means "move data from device to memory"
	 * so when (write) is true, it actually means READ!
	 */
	if (write)
		dma_init_read(esp, addr, count);
	else
		dma_init_write(esp, addr, count);
}

static void dma_mmu_get_scsi_one(struct NCR_ESP *esp, struct scsi_cmnd * sp)
{
	sp->SCp.ptr = (char *)virt_to_phys(sp->request_buffer);
}

static void dma_mmu_get_scsi_sgl(struct NCR_ESP *esp, struct scsi_cmnd * sp)
{
	int sz = sp->SCp.buffers_residual;
	struct scatterlist *sg = sp->SCp.buffer;

	while (sz >= 0) {
		sg[sz].dma_address = page_to_phys(sg[sz].page) + sg[sz].offset;
		sz--;
	}
	sp->SCp.ptr = (char *)(sp->SCp.buffer->dma_address);
}

static void dma_advance_sg(struct scsi_cmnd * sp)
{
	sp->SCp.ptr = (char *)(sp->SCp.buffer->dma_address);
}

static void pmaz_dma_drain(struct NCR_ESP *esp)
{
	memcpy(phys_to_virt(esp_virt_buffer),
	       (void *)CKSEG1ADDR(esp->slot + DEC_SCSI_SRAM +
				  ESP_TGT_DMA_SIZE),
	       scsi_current_length);
}

static void pmaz_dma_init_read(struct NCR_ESP *esp, u32 vaddress, int length)
{
	volatile u32 *dmareg =
		(volatile u32 *)CKSEG1ADDR(esp->slot + DEC_SCSI_DMAREG);

	if (length > ESP_TGT_DMA_SIZE)
		length = ESP_TGT_DMA_SIZE;

	*dmareg = TC_ESP_DMA_ADDR(ESP_TGT_DMA_SIZE);

	iob();

	esp_virt_buffer = vaddress;
	scsi_current_length = length;
}

static void pmaz_dma_init_write(struct NCR_ESP *esp, u32 vaddress, int length)
{
	volatile u32 *dmareg =
		(volatile u32 *)CKSEG1ADDR(esp->slot + DEC_SCSI_DMAREG);

	memcpy((void *)CKSEG1ADDR(esp->slot + DEC_SCSI_SRAM +
				  ESP_TGT_DMA_SIZE),
	       phys_to_virt(vaddress), length);

	wmb();
	*dmareg = TC_ESP_DMAR_WRITE | TC_ESP_DMA_ADDR(ESP_TGT_DMA_SIZE);

	iob();
}

static void pmaz_dma_ints_off(struct NCR_ESP *esp)
{
}

static void pmaz_dma_ints_on(struct NCR_ESP *esp)
{
}

static void pmaz_dma_setup(struct NCR_ESP *esp, u32 addr, int count, int write)
{
	/*
	 * DMA_ST_WRITE means "move data from device to memory"
	 * so when (write) is true, it actually means READ!
	 */
	if (write)
		pmaz_dma_init_read(esp, addr, count);
	else
		pmaz_dma_init_write(esp, addr, count);
}

static void pmaz_dma_mmu_get_scsi_one(struct NCR_ESP *esp, struct scsi_cmnd * sp)
{
	sp->SCp.ptr = (char *)virt_to_phys(sp->request_buffer);
}
