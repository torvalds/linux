/* Wrapper for DMA channel allocator that updates DMA client muxing.
 * Copyright 2004-2007, Axis Communications AB
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <asm/dma.h>
#include <asm/arch/svinto.h>

/* Macro to access ETRAX 100 registers */
#define SETS(var, reg, field, val) var = (var & ~IO_MASK_(reg##_, field##_)) | \
					  IO_STATE_(reg##_, field##_, _##val)


static char used_dma_channels[MAX_DMA_CHANNELS];
static const char * used_dma_channels_users[MAX_DMA_CHANNELS];

int cris_request_dma(unsigned int dmanr, const char * device_id,
		     unsigned options, enum dma_owner owner)
{
	unsigned long flags;
	unsigned long int gens;
	int fail = -EINVAL;

	if ((dmanr < 0) || (dmanr >= MAX_DMA_CHANNELS)) {
		printk(KERN_CRIT "cris_request_dma: invalid DMA channel %u\n", dmanr);
		return -EINVAL;
	}

	local_irq_save(flags);
	if (used_dma_channels[dmanr]) {
		local_irq_restore(flags);
		if (options & DMA_VERBOSE_ON_ERROR) {
			printk(KERN_CRIT "Failed to request DMA %i for %s, already allocated by %s\n", dmanr, device_id, used_dma_channels_users[dmanr]);
		}
		if (options & DMA_PANIC_ON_ERROR) {
			panic("request_dma error!");
		}
		return -EBUSY;
	}

	gens = genconfig_shadow;

	switch(owner)
	{
	case dma_eth:
		if ((dmanr != NETWORK_TX_DMA_NBR) &&
		    (dmanr != NETWORK_RX_DMA_NBR)) {
			printk(KERN_CRIT "Invalid DMA channel for eth\n");
			goto bail;
		}
		break;
	case dma_ser0:
		if (dmanr == SER0_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma6, serial0);
		} else if (dmanr == SER0_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma7, serial0);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ser0\n");
			goto bail;
		}
		break;
	case dma_ser1:
		if (dmanr == SER1_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma8, serial1);
		} else if (dmanr == SER1_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma9, serial1);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ser1\n");
			goto bail;
		}
		break;
	case dma_ser2:
		if (dmanr == SER2_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma2, serial2);
		} else if (dmanr == SER2_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma3, serial2);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ser2\n");
			goto bail;
		}
		break;
	case dma_ser3:
		if (dmanr == SER3_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma4, serial3);
		} else if (dmanr == SER3_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma5, serial3);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ser3\n");
			goto bail;
		}
		break;
	case dma_ata:
		if (dmanr == ATA_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma2, ata);
		} else if (dmanr == ATA_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma3, ata);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ata\n");
			goto bail;
		}
		break;
	case dma_ext0:
		if (dmanr == EXTDMA0_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma4, extdma0);
		} else if (dmanr == EXTDMA0_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma5, extdma0);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ext0\n");
			goto bail;
		}
		break;
	case dma_ext1:
		if (dmanr == EXTDMA1_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma6, extdma1);
		} else if (dmanr == EXTDMA1_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma7, extdma1);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for ext1\n");
			goto bail;
		}
		break;
	case dma_int6:
		if (dmanr == MEM2MEM_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma7, intdma6);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for int6\n");
			goto bail;
		}
		break;
	case dma_int7:
		if (dmanr == MEM2MEM_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma6, intdma7);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for int7\n");
			goto bail;
		}
		break;
	case dma_usb:
		if (dmanr == USB_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma8, usb);
		} else if (dmanr == USB_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma9, usb);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for usb\n");
			goto bail;
		}
		break;
	case dma_scsi0:
		if (dmanr == SCSI0_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma2, scsi0);
		} else if (dmanr == SCSI0_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma3, scsi0);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for scsi0\n");
			goto bail;
		}
		break;
	case dma_scsi1:
		if (dmanr == SCSI1_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma4, scsi1);
		} else if (dmanr == SCSI1_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma5, scsi1);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for scsi1\n");
			goto bail;
		}
		break;
	case dma_par0:
		if (dmanr == PAR0_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma2, par0);
		} else if (dmanr == PAR0_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma3, par0);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for par0\n");
			goto bail;
		}
		break;
	case dma_par1:
		if (dmanr == PAR1_TX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma4, par1);
		} else if (dmanr == PAR1_RX_DMA_NBR) {
			SETS(gens, R_GEN_CONFIG, dma5, par1);
		} else {
			printk(KERN_CRIT "Invalid DMA channel for par1\n");
			goto bail;
		}
		break;
	default:
		printk(KERN_CRIT "Invalid DMA owner.\n");
		goto bail;
	}

	used_dma_channels[dmanr] = 1;
	used_dma_channels_users[dmanr] = device_id;

	{
		volatile int i;
		genconfig_shadow = gens;
		*R_GEN_CONFIG = genconfig_shadow;
		/* Wait 12 cycles before doing any DMA command */
		for(i = 6; i > 0; i--)
			nop();
	}
	fail = 0;
 bail:
	local_irq_restore(flags);
	return fail;
}

void cris_free_dma(unsigned int dmanr, const char * device_id)
{
	unsigned long flags;
	if ((dmanr < 0) || (dmanr >= MAX_DMA_CHANNELS)) {
		printk(KERN_CRIT "cris_free_dma: invalid DMA channel %u\n", dmanr);
		return;
	}

	local_irq_save(flags);
	if (!used_dma_channels[dmanr]) {
		printk(KERN_CRIT "cris_free_dma: DMA channel %u not allocated\n", dmanr);
	} else if (device_id != used_dma_channels_users[dmanr]) {
		printk(KERN_CRIT "cris_free_dma: DMA channel %u not allocated by device\n", dmanr);
	} else {
		switch(dmanr)
		{
		case 0:
			*R_DMA_CH0_CMD = IO_STATE(R_DMA_CH0_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH0_CMD, cmd, *R_DMA_CH0_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH0_CMD, cmd, reset));
			break;
		case 1:
			*R_DMA_CH1_CMD = IO_STATE(R_DMA_CH1_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH1_CMD, cmd, *R_DMA_CH1_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH1_CMD, cmd, reset));
			break;
		case 2:
			*R_DMA_CH2_CMD = IO_STATE(R_DMA_CH2_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH2_CMD, cmd, *R_DMA_CH2_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH2_CMD, cmd, reset));
			break;
		case 3:
			*R_DMA_CH3_CMD = IO_STATE(R_DMA_CH3_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH3_CMD, cmd, *R_DMA_CH3_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH3_CMD, cmd, reset));
			break;
		case 4:
			*R_DMA_CH4_CMD = IO_STATE(R_DMA_CH4_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH4_CMD, cmd, *R_DMA_CH4_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH4_CMD, cmd, reset));
			break;
		case 5:
			*R_DMA_CH5_CMD = IO_STATE(R_DMA_CH5_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH5_CMD, cmd, *R_DMA_CH5_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH5_CMD, cmd, reset));
			break;
		case 6:
			*R_DMA_CH6_CMD = IO_STATE(R_DMA_CH6_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH6_CMD, cmd, *R_DMA_CH6_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH6_CMD, cmd, reset));
			break;
		case 7:
			*R_DMA_CH7_CMD = IO_STATE(R_DMA_CH7_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH7_CMD, cmd, *R_DMA_CH7_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH7_CMD, cmd, reset));
			break;
		case 8:
			*R_DMA_CH8_CMD = IO_STATE(R_DMA_CH8_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH8_CMD, cmd, *R_DMA_CH8_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH8_CMD, cmd, reset));
			break;
		case 9:
			*R_DMA_CH9_CMD = IO_STATE(R_DMA_CH9_CMD, cmd, reset);
			while (IO_EXTRACT(R_DMA_CH9_CMD, cmd, *R_DMA_CH9_CMD) ==
			       IO_STATE_VALUE(R_DMA_CH9_CMD, cmd, reset));
			break;
		}
		used_dma_channels[dmanr] = 0;
	}
	local_irq_restore(flags);
}

EXPORT_SYMBOL(cris_request_dma);
EXPORT_SYMBOL(cris_free_dma);
