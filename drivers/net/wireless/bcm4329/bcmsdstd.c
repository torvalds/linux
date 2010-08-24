/*
 *  'Standard' SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmsdstd.c,v 1.64.4.1.4.4.2.18 2010/08/17 17:00:48 Exp $
 */

#include <typedefs.h>

#include <bcmdevs.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <osl.h>
#include <siutils.h>
#include <sdio.h>	/* SDIO Device and Protocol Specs */
#include <sdioh.h>	/* SDIO Host Controller Specification */
#include <bcmsdbus.h>	/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>	/* ioctl/iovars */
#include <pcicfg.h>


#define SD_PAGE_BITS	12
#define SD_PAGE 	(1 << SD_PAGE_BITS)

#include <bcmsdstd.h>

/* Globals */
uint sd_msglevel = SDH_ERROR_VAL;
uint sd_hiok = TRUE;			/* Use hi-speed mode if available? */
uint sd_sdmode = SDIOH_MODE_SD4;	/* Use SD4 mode by default */
uint sd_f2_blocksize = 64;		/* Default blocksize */

#ifdef BCMSDYIELD
bool sd_yieldcpu = TRUE;		/* Allow CPU yielding for buffer requests */
uint sd_minyield = 0;			/* Minimum xfer size to allow CPU yield */
bool sd_forcerb = FALSE;		/* Force sync readback in intrs_on/off */
#endif

uint sd_divisor = 2;			/* Default 48MHz/2 = 24MHz */

uint sd_power = 1;		/* Default to SD Slot powered ON */
uint sd_clock = 1;		/* Default to SD Clock turned ON */
uint sd_pci_slot = 0xFFFFffff; /* Used to force selection of a particular PCI slot */
uint8 sd_dma_mode = DMA_MODE_SDMA; /* Default to SDMA for now */

uint sd_toctl = 7;

static bool trap_errs = FALSE;

static const char *dma_mode_description[] = { "PIO", "SDMA", "ADMA1", "32b ADMA2", "64b ADMA2" };

/* Prototypes */
static bool sdstd_start_clock(sdioh_info_t *sd, uint16 divisor);
static bool sdstd_start_power(sdioh_info_t *sd);
static bool sdstd_bus_width(sdioh_info_t *sd, int width);
static int sdstd_set_highspeed_mode(sdioh_info_t *sd, bool HSMode);
static int sdstd_set_dma_mode(sdioh_info_t *sd, int8 dma_mode);
static int sdstd_card_enablefuncs(sdioh_info_t *sd);
static void sdstd_cmd_getrsp(sdioh_info_t *sd, uint32 *rsp_buffer, int count);
static int sdstd_cmd_issue(sdioh_info_t *sd, bool use_dma, uint32 cmd, uint32 arg);
static int sdstd_card_regread(sdioh_info_t *sd, int func, uint32 regaddr,
                              int regsize, uint32 *data);
static int sdstd_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr,
                               int regsize, uint32 data);
static int sdstd_driver_init(sdioh_info_t *sd);
static bool sdstd_reset(sdioh_info_t *sd, bool host_reset, bool client_reset);
static int sdstd_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo,
                          uint32 addr, int nbytes, uint32 *data);
static int sdstd_abort(sdioh_info_t *sd, uint func);
static int sdstd_check_errs(sdioh_info_t *sdioh_info, uint32 cmd, uint32 arg);
static int set_client_block_size(sdioh_info_t *sd, int func, int blocksize);
static void sd_map_dma(sdioh_info_t * sd);
static void sd_unmap_dma(sdioh_info_t * sd);
static void sd_clear_adma_dscr_buf(sdioh_info_t *sd);
static void sd_fill_dma_data_buf(sdioh_info_t *sd, uint8 data);
static void sd_create_adma_descriptor(sdioh_info_t *sd,
                                      uint32 index, uint32 addr_phys,
                                      uint16 length, uint16 flags);
static void sd_dump_adma_dscr(sdioh_info_t *sd);
static void sdstd_dumpregs(sdioh_info_t *sd);


/*
 * Private register access routines.
 */

/* 16 bit PCI regs */

extern uint16 sdstd_rreg16(sdioh_info_t *sd, uint reg);
uint16
sdstd_rreg16(sdioh_info_t *sd, uint reg)
{

	volatile uint16 data = *(volatile uint16 *)(sd->mem_space + reg);
	sd_ctrl(("16: R Reg 0x%02x, Data 0x%x\n", reg, data));
	return data;
}

extern void sdstd_wreg16(sdioh_info_t *sd, uint reg, uint16 data);
void
sdstd_wreg16(sdioh_info_t *sd, uint reg, uint16 data)
{
	*(volatile uint16 *)(sd->mem_space + reg) = (uint16)data;
	sd_ctrl(("16: W Reg 0x%02x, Data 0x%x\n", reg, data));
}

static void
sdstd_or_reg16(sdioh_info_t *sd, uint reg, uint16 val)
{
	volatile uint16 data = *(volatile uint16 *)(sd->mem_space + reg);
	sd_ctrl(("16: OR Reg 0x%02x, Val 0x%x\n", reg, val));
	data |= val;
	*(volatile uint16 *)(sd->mem_space + reg) = (uint16)data;

}
static void
sdstd_mod_reg16(sdioh_info_t *sd, uint reg, int16 mask, uint16 val)
{

	volatile uint16 data = *(volatile uint16 *)(sd->mem_space + reg);
	sd_ctrl(("16: MOD Reg 0x%02x, Mask 0x%x, Val 0x%x\n", reg, mask, val));
	data &= ~mask;
	data |= (val & mask);
	*(volatile uint16 *)(sd->mem_space + reg) = (uint16)data;
}


/* 32 bit PCI regs */
static uint32
sdstd_rreg(sdioh_info_t *sd, uint reg)
{
	volatile uint32 data = *(volatile uint32 *)(sd->mem_space + reg);
	sd_ctrl(("32: R Reg 0x%02x, Data 0x%x\n", reg, data));
	return data;
}
static inline void
sdstd_wreg(sdioh_info_t *sd, uint reg, uint32 data)
{
	*(volatile uint32 *)(sd->mem_space + reg) = (uint32)data;
	sd_ctrl(("32: W Reg 0x%02x, Data 0x%x\n", reg, data));

}

/* 8 bit PCI regs */
static inline void
sdstd_wreg8(sdioh_info_t *sd, uint reg, uint8 data)
{
	*(volatile uint8 *)(sd->mem_space + reg) = (uint8)data;
	sd_ctrl(("08: W Reg 0x%02x, Data 0x%x\n", reg, data));
}
static uint8
sdstd_rreg8(sdioh_info_t *sd, uint reg)
{
	volatile uint8 data = *(volatile uint8 *)(sd->mem_space + reg);
	sd_ctrl(("08: R Reg 0x%02x, Data 0x%x\n", reg, data));
	return data;
}

/*
 * Private work routines
 */

sdioh_info_t *glob_sd;

/*
 *  Public entry points & extern's
 */
extern sdioh_info_t *
sdioh_attach(osl_t *osh, void *bar0, uint irq)
{
	sdioh_info_t *sd;

	sd_trace(("%s\n", __FUNCTION__));
	if ((sd = (sdioh_info_t *)MALLOC(osh, sizeof(sdioh_info_t))) == NULL) {
		sd_err(("sdioh_attach: out of memory, malloced %d bytes\n", MALLOCED(osh)));
		return NULL;
	}
	bzero((char *)sd, sizeof(sdioh_info_t));
	glob_sd = sd;
	sd->osh = osh;
	if (sdstd_osinit(sd) != 0) {
		sd_err(("%s:sdstd_osinit() failed\n", __FUNCTION__));
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return NULL;
	}
	sd->mem_space = (volatile char *)sdstd_reg_map(osh, (uintptr)bar0, SDIOH_REG_WINSZ);
	sd_init_dma(sd);
	sd->irq = irq;
	if (sd->mem_space == NULL) {
		sd_err(("%s:ioremap() failed\n", __FUNCTION__));
		sdstd_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return NULL;
	}
	sd_info(("%s:sd->mem_space = %p\n", __FUNCTION__, sd->mem_space));
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;
	sd->intr_handler_valid = FALSE;

	/* Set defaults */
	sd->sd_blockmode = TRUE;
	sd->use_client_ints = TRUE;
	sd->sd_dma_mode = sd_dma_mode;

	if (!sd->sd_blockmode)
		sd->sd_dma_mode = DMA_MODE_NONE;

	if (sdstd_driver_init(sd) != SUCCESS) {
		/* If host CPU was reset without resetting SD bus or
		   SD device, the device will still have its RCA but
		   driver no longer knows what it is (since driver has been restarted).
		   go through once to clear the RCA and a gain reassign it.
		 */
		sd_info(("driver_init failed - Reset RCA and try again\n"));
		if (sdstd_driver_init(sd) != SUCCESS) {
			sd_err(("%s:driver_init() failed()\n", __FUNCTION__));
			if (sd->mem_space) {
				sdstd_reg_unmap(osh, (uintptr)sd->mem_space, SDIOH_REG_WINSZ);
				sd->mem_space = NULL;
			}
			sdstd_osfree(sd);
			MFREE(sd->osh, sd, sizeof(sdioh_info_t));
			return (NULL);
		}
	}

	OSL_DMADDRWIDTH(osh, 32);

	/* Always map DMA buffers, so we can switch between DMA modes. */
	sd_map_dma(sd);

	if (sdstd_register_irq(sd, irq) != SUCCESS) {
		sd_err(("%s: sdstd_register_irq() failed for irq = %d\n", __FUNCTION__, irq));
		sdstd_free_irq(sd->irq, sd);
		if (sd->mem_space) {
			sdstd_reg_unmap(osh, (uintptr)sd->mem_space, SDIOH_REG_WINSZ);
			sd->mem_space = NULL;
		}

		sdstd_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return (NULL);
	}

	sd_trace(("%s: Done\n", __FUNCTION__));
	return sd;
}

extern SDIOH_API_RC
sdioh_detach(osl_t *osh, sdioh_info_t *sd)
{
	sd_trace(("%s\n", __FUNCTION__));
	if (sd) {
		sd_unmap_dma(sd);
		sdstd_wreg16(sd, SD_IntrSignalEnable, 0);
		sd_trace(("%s: freeing irq %d\n", __FUNCTION__, sd->irq));
		sdstd_free_irq(sd->irq, sd);
		if (sd->card_init_done)
			sdstd_reset(sd, 1, 1);
		if (sd->mem_space) {
			sdstd_reg_unmap(osh, (uintptr)sd->mem_space, SDIOH_REG_WINSZ);
			sd->mem_space = NULL;
		}

		sdstd_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
	}
	return SDIOH_API_RC_SUCCESS;
}

/* Configure callback to client when we receive client interrupt */
extern SDIOH_API_RC
sdioh_interrupt_register(sdioh_info_t *sd, sdioh_cb_fn_t fn, void *argh)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
	sd->intr_handler = fn;
	sd->intr_handler_arg = argh;
	sd->intr_handler_valid = TRUE;
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_interrupt_deregister(sdioh_info_t *sd)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
	sd->intr_handler_valid = FALSE;
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_interrupt_query(sdioh_info_t *sd, bool *onoff)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
	*onoff = sd->client_intr_enabled;
	return SDIOH_API_RC_SUCCESS;
}

#if defined(DHD_DEBUG)
extern bool
sdioh_interrupt_pending(sdioh_info_t *sd)
{
	uint16 intrstatus;
	intrstatus = sdstd_rreg16(sd, SD_IntrStatus);
	return !!(intrstatus & CLIENT_INTR);
}
#endif

uint
sdioh_query_iofnum(sdioh_info_t *sd)
{
	return sd->num_funcs;
}

/* IOVar table */
enum {
	IOV_MSGLEVEL = 1,
	IOV_BLOCKMODE,
	IOV_BLOCKSIZE,
	IOV_DMA,
	IOV_USEINTS,
	IOV_NUMINTS,
	IOV_NUMLOCALINTS,
	IOV_HOSTREG,
	IOV_DEVREG,
	IOV_DIVISOR,
	IOV_SDMODE,
	IOV_HISPEED,
	IOV_HCIREGS,
	IOV_POWER,
	IOV_YIELDCPU,
	IOV_MINYIELD,
	IOV_FORCERB,
	IOV_CLOCK
};

const bcm_iovar_t sdioh_iovars[] = {
	{"sd_msglevel",	IOV_MSGLEVEL, 	0,	IOVT_UINT32,	0 },
	{"sd_blockmode", IOV_BLOCKMODE,	0,	IOVT_BOOL,	0 },
	{"sd_blocksize", IOV_BLOCKSIZE, 0,	IOVT_UINT32,	0 }, /* ((fn << 16) | size) */
	{"sd_dma",	IOV_DMA,	0,	IOVT_UINT32,	0 },
#ifdef BCMSDYIELD
	{"sd_yieldcpu",	IOV_YIELDCPU,	0,	IOVT_BOOL,	0 },
	{"sd_minyield",	IOV_MINYIELD,	0,	IOVT_UINT32,	0 },
	{"sd_forcerb",	IOV_FORCERB,	0,	IOVT_BOOL,	0 },
#endif
	{"sd_ints",	IOV_USEINTS,	0,	IOVT_BOOL,	0 },
	{"sd_numints",	IOV_NUMINTS,	0,	IOVT_UINT32,	0 },
	{"sd_numlocalints", IOV_NUMLOCALINTS, 0, IOVT_UINT32,	0 },
	{"sd_hostreg",	IOV_HOSTREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sd_devreg",	IOV_DEVREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t)	},
	{"sd_divisor",	IOV_DIVISOR,	0,	IOVT_UINT32,	0 },
	{"sd_power",	IOV_POWER,	0,	IOVT_UINT32,	0 },
	{"sd_clock",	IOV_CLOCK,	0,	IOVT_UINT32,	0 },
	{"sd_mode",	IOV_SDMODE,	0,	IOVT_UINT32,	100},
	{"sd_highspeed",	IOV_HISPEED,	0,	IOVT_UINT32,	0},
	{NULL, 0, 0, 0, 0 }
};

int
sdioh_iovar_op(sdioh_info_t *si, const char *name,
               void *params, int plen, void *arg, int len, bool set)
{
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	int32 int_val = 0;
	bool bool_val;
	uint32 actionid;

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get must have return space; Set does not take qualifiers */
	ASSERT(set || (arg && len));
	ASSERT(!set || (!params && !plen));

	sd_trace(("%s: Enter (%s %s)\n", __FUNCTION__, (set ? "set" : "get"), name));

	if ((vi = bcm_iovar_lookup(sdioh_iovars, name)) == NULL) {
		bcmerror = BCME_UNSUPPORTED;
		goto exit;
	}

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, set)) != 0)
		goto exit;

	/* Set up params so get and set can share the convenience variables */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		val_size = sizeof(int);

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	bool_val = (int_val != 0) ? TRUE : FALSE;

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	switch (actionid) {
	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (int32)sd_msglevel;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		sd_msglevel = int_val;
		break;

	case IOV_GVAL(IOV_BLOCKMODE):
		int_val = (int32)si->sd_blockmode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_BLOCKMODE):
		si->sd_blockmode = (bool)int_val;
		/* Haven't figured out how to make non-block mode with DMA */
		if (!si->sd_blockmode)
			si->sd_dma_mode = DMA_MODE_NONE;
		break;

#ifdef BCMSDYIELD
	case IOV_GVAL(IOV_YIELDCPU):
		int_val = sd_yieldcpu;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_YIELDCPU):
		sd_yieldcpu = (bool)int_val;
		break;

	case IOV_GVAL(IOV_MINYIELD):
		int_val = sd_minyield;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MINYIELD):
		sd_minyield = (bool)int_val;
		break;

	case IOV_GVAL(IOV_FORCERB):
		int_val = sd_forcerb;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_FORCERB):
		sd_forcerb = (bool)int_val;
		break;
#endif /* BCMSDYIELD */

	case IOV_GVAL(IOV_BLOCKSIZE):
		if ((uint32)int_val > si->num_funcs) {
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = (int32)si->client_block_size[int_val];
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_BLOCKSIZE):
	{
		uint func = ((uint32)int_val >> 16);
		uint blksize = (uint16)int_val;
		uint maxsize;

		if (func > si->num_funcs) {
			bcmerror = BCME_BADARG;
			break;
		}

		switch (func) {
		case 0: maxsize = 32; break;
		case 1: maxsize = BLOCK_SIZE_4318; break;
		case 2: maxsize = BLOCK_SIZE_4328; break;
		default: maxsize = 0;
		}
		if (blksize > maxsize) {
			bcmerror = BCME_BADARG;
			break;
		}
		if (!blksize) {
			blksize = maxsize;
		}

		/* Now set it */
		sdstd_lock(si);
		bcmerror = set_client_block_size(si, func, blksize);
		sdstd_unlock(si);
		break;
	}

	case IOV_GVAL(IOV_DMA):
		int_val = (int32)si->sd_dma_mode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DMA):
		si->sd_dma_mode = (char)int_val;
		sdstd_set_dma_mode(si, si->sd_dma_mode);
		break;

	case IOV_GVAL(IOV_USEINTS):
		int_val = (int32)si->use_client_ints;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_USEINTS):
		si->use_client_ints = (bool)int_val;
		if (si->use_client_ints)
			si->intmask |= CLIENT_INTR;
		else
			si->intmask &= ~CLIENT_INTR;
		break;

	case IOV_GVAL(IOV_DIVISOR):
		int_val = (uint32)sd_divisor;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DIVISOR):
		sd_divisor = int_val;
		if (!sdstd_start_clock(si, (uint16)sd_divisor)) {
			sd_err(("set clock failed!\n"));
			bcmerror = BCME_ERROR;
		}
		break;

	case IOV_GVAL(IOV_POWER):
		int_val = (uint32)sd_power;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_POWER):
		sd_power = int_val;
		if (sd_power == 1) {
			if (sdstd_driver_init(si) != SUCCESS) {
				sd_err(("set SD Slot power failed!\n"));
				bcmerror = BCME_ERROR;
			} else {
				sd_err(("SD Slot Powered ON.\n"));
			}
		} else {
			uint8 pwr = 0;

			pwr = SFIELD(pwr, PWR_BUS_EN, 0);
			sdstd_wreg8(si, SD_PwrCntrl, pwr); /* Set Voltage level */
			sd_err(("SD Slot Powered OFF.\n"));
		}
		break;

	case IOV_GVAL(IOV_CLOCK):
		int_val = (uint32)sd_clock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CLOCK):
		sd_clock = int_val;
		if (sd_clock == 1) {
			sd_info(("SD Clock turned ON.\n"));
			if (!sdstd_start_clock(si, (uint16)sd_divisor)) {
				sd_err(("sdstd_start_clock failed\n"));
				bcmerror = BCME_ERROR;
			}
		} else {
			/* turn off HC clock */
			sdstd_wreg16(si, SD_ClockCntrl,
			             sdstd_rreg16(si, SD_ClockCntrl) & ~((uint16)0x4));

			sd_info(("SD Clock turned OFF.\n"));
		}
		break;

	case IOV_GVAL(IOV_SDMODE):
		int_val = (uint32)sd_sdmode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDMODE):
		sd_sdmode = int_val;

		if (!sdstd_bus_width(si, sd_sdmode)) {
			sd_err(("sdstd_bus_width failed\n"));
			bcmerror = BCME_ERROR;
		}
		break;

	case IOV_GVAL(IOV_HISPEED):
		int_val = (uint32)sd_hiok;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HISPEED):
		sd_hiok = int_val;
		bcmerror = sdstd_set_highspeed_mode(si, (bool)sd_hiok);
		break;

	case IOV_GVAL(IOV_NUMINTS):
		int_val = (int32)si->intrcount;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_NUMLOCALINTS):
		int_val = (int32)si->local_intrcount;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_HOSTREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;

		if (sd_ptr->offset < SD_SysAddr || sd_ptr->offset > SD_MaxCurCap) {
			sd_err(("%s: bad offset 0x%x\n", __FUNCTION__, sd_ptr->offset));
			bcmerror = BCME_BADARG;
			break;
		}

		sd_trace(("%s: rreg%d at offset %d\n", __FUNCTION__,
		          (sd_ptr->offset & 1) ? 8 : ((sd_ptr->offset & 2) ? 16 : 32),
		          sd_ptr->offset));
		if (sd_ptr->offset & 1)
			int_val = sdstd_rreg8(si, sd_ptr->offset);
		else if (sd_ptr->offset & 2)
			int_val = sdstd_rreg16(si, sd_ptr->offset);
		else
			int_val = sdstd_rreg(si, sd_ptr->offset);

		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_HOSTREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;

		if (sd_ptr->offset < SD_SysAddr || sd_ptr->offset > SD_MaxCurCap) {
			sd_err(("%s: bad offset 0x%x\n", __FUNCTION__, sd_ptr->offset));
			bcmerror = BCME_BADARG;
			break;
		}

		sd_trace(("%s: wreg%d value 0x%08x at offset %d\n", __FUNCTION__, sd_ptr->value,
		          (sd_ptr->offset & 1) ? 8 : ((sd_ptr->offset & 2) ? 16 : 32),
		          sd_ptr->offset));
		if (sd_ptr->offset & 1)
			sdstd_wreg8(si, sd_ptr->offset, (uint8)sd_ptr->value);
		else if (sd_ptr->offset & 2)
			sdstd_wreg16(si, sd_ptr->offset, (uint16)sd_ptr->value);
		else
			sdstd_wreg(si, sd_ptr->offset, (uint32)sd_ptr->value);

		break;
	}

	case IOV_GVAL(IOV_DEVREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;
		uint8 data;

		if (sdioh_cfg_read(si, sd_ptr->func, sd_ptr->offset, &data)) {
			bcmerror = BCME_SDIO_ERROR;
			break;
		}

		int_val = (int)data;
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_DEVREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;
		uint8 data = (uint8)sd_ptr->value;

		if (sdioh_cfg_write(si, sd_ptr->func, sd_ptr->offset, &data)) {
			bcmerror = BCME_SDIO_ERROR;
			break;
		}
		break;
	}


	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}
exit:

	return bcmerror;
}

extern SDIOH_API_RC
sdioh_cfg_read(sdioh_info_t *sd, uint fnc_num, uint32 addr, uint8 *data)
{
	SDIOH_API_RC status;
	/* No lock needed since sdioh_request_byte does locking */
	status = sdioh_request_byte(sd, SDIOH_READ, fnc_num, addr, data);
	return status;
}

extern SDIOH_API_RC
sdioh_cfg_write(sdioh_info_t *sd, uint fnc_num, uint32 addr, uint8 *data)
{
	/* No lock needed since sdioh_request_byte does locking */
	SDIOH_API_RC status;
	status = sdioh_request_byte(sd, SDIOH_WRITE, fnc_num, addr, data);
	return status;
}

extern SDIOH_API_RC
sdioh_cis_read(sdioh_info_t *sd, uint func, uint8 *cisd, uint32 length)
{
	uint32 count;
	int offset;
	uint32 foo;
	uint8 *cis = cisd;

	sd_trace(("%s: Func = %d\n", __FUNCTION__, func));

	if (!sd->func_cis_ptr[func]) {
		bzero(cis, length);
		return SDIOH_API_RC_FAIL;
	}

	sdstd_lock(sd);
	*cis = 0;
	for (count = 0; count < length; count++) {
		offset =  sd->func_cis_ptr[func] + count;
		if (sdstd_card_regread(sd, 0, offset, 1, &foo)) {
			sd_err(("%s: regread failed: Can't read CIS\n", __FUNCTION__));
			sdstd_unlock(sd);
			return SDIOH_API_RC_FAIL;
		}
		*cis = (uint8)(foo & 0xff);
		cis++;
	}
	sdstd_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_request_byte(sdioh_info_t *sd, uint rw, uint func, uint regaddr, uint8 *byte)
{
	int status;
	uint32 cmd_arg;
	uint32 rsp5;

	sdstd_lock(sd);
	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, CMD52_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, CMD52_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, CMD52_RW_FLAG, rw == SDIOH_READ ? 0 : 1);
	cmd_arg = SFIELD(cmd_arg, CMD52_RAW, 0);
	cmd_arg = SFIELD(cmd_arg, CMD52_DATA, rw == SDIOH_READ ? 0 : *byte);

	if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_52, cmd_arg)) != SUCCESS) {
		sdstd_unlock(sd);
		return status;
	}

	sdstd_cmd_getrsp(sd, &rsp5, 1);
	if (sdstd_rreg16 (sd, SD_ErrorIntrStatus) != 0) {
		sd_err(("%s: 1: ErrorintrStatus 0x%x\n",
		        __FUNCTION__, sdstd_rreg16(sd, SD_ErrorIntrStatus)));
	}
	if (GFIELD(rsp5, RSP5_FLAGS) != 0x10)
		sd_err(("%s: rsp5 flags is 0x%x\t %d\n",
		        __FUNCTION__, GFIELD(rsp5, RSP5_FLAGS), func));

	if (GFIELD(rsp5, RSP5_STUFF))
		sd_err(("%s: rsp5 stuff is 0x%x: should be 0\n",
		        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));

	if (rw == SDIOH_READ)
		*byte = GFIELD(rsp5, RSP5_DATA);

	sdstd_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_request_word(sdioh_info_t *sd, uint cmd_type, uint rw, uint func, uint addr,
                   uint32 *word, uint nbytes)
{
	int status;
	bool swap = FALSE;

	sdstd_lock(sd);

	if (rw == SDIOH_READ) {
		status = sdstd_card_regread(sd, func, addr, nbytes, word);
		if (swap)
			*word = BCMSWAP32(*word);
	} else {
		if (swap)
			*word = BCMSWAP32(*word);
		status = sdstd_card_regwrite(sd, func, addr, nbytes, *word);
	}

	sdstd_unlock(sd);
	return (status == SUCCESS ?  SDIOH_API_RC_SUCCESS : SDIOH_API_RC_FAIL);
}

extern SDIOH_API_RC
sdioh_request_buffer(sdioh_info_t *sd, uint pio_dma, uint fix_inc, uint rw, uint func,
                     uint addr, uint reg_width, uint buflen_u, uint8 *buffer, void *pkt)
{
	int len;
	int buflen = (int)buflen_u;
	bool fifo = (fix_inc == SDIOH_DATA_FIX);
	uint8 *localbuf = NULL, *tmpbuf = NULL;
	uint tmplen = 0;
	bool local_blockmode = sd->sd_blockmode;

	sdstd_lock(sd);

	ASSERT(reg_width == 4);
	ASSERT(buflen_u < (1 << 30));
	ASSERT(sd->client_block_size[func]);

	sd_data(("%s: %c len %d r_cnt %d t_cnt %d, pkt @0x%p\n",
	         __FUNCTION__, rw == SDIOH_READ ? 'R' : 'W',
	         buflen_u, sd->r_cnt, sd->t_cnt, pkt));

	/* Break buffer down into blocksize chunks:
	 * Bytemode: 1 block at a time.
	 * Blockmode: Multiples of blocksizes at a time w/ max of SD_PAGE.
	 * Both: leftovers are handled last (will be sent via bytemode).
	 */
	while (buflen > 0) {
		if (local_blockmode) {
			/* Max xfer is Page size */
			len = MIN(SD_PAGE, buflen);

			/* Round down to a block boundry */
			if (buflen > sd->client_block_size[func])
				len = (len/sd->client_block_size[func]) *
				        sd->client_block_size[func];
			if ((func == SDIO_FUNC_1) && ((len % 4) == 3) && (rw == SDIOH_WRITE)) {
				tmplen = len;
				sd_err(("%s: Rounding up buffer to mod4 length.\n", __FUNCTION__));
				len++;
				tmpbuf = buffer;
				if ((localbuf = (uint8 *)MALLOC(sd->osh, len)) == NULL) {
					sd_err(("out of memory, malloced %d bytes\n",
					        MALLOCED(sd->osh)));
					sdstd_unlock(sd);
					return SDIOH_API_RC_FAIL;
				}
				bcopy(buffer, localbuf, len);
				buffer = localbuf;
			}
		} else {
			/* Byte mode: One block at a time */
			len = MIN(sd->client_block_size[func], buflen);
		}

		if (sdstd_card_buf(sd, rw, func, fifo, addr, len, (uint32 *)buffer) != SUCCESS) {
			sdstd_unlock(sd);
			return SDIOH_API_RC_FAIL;
		}

		if (local_blockmode) {
			if ((func == SDIO_FUNC_1) && ((tmplen % 4) == 3) && (rw == SDIOH_WRITE)) {
				if (localbuf)
					MFREE(sd->osh, localbuf, len);
				len--;
				buffer = tmpbuf;
				sd_err(("%s: Restoring back buffer ptr and len.\n", __FUNCTION__));
			}
		}

		buffer += len;
		buflen -= len;
		if (!fifo)
			addr += len;
	}
	sdstd_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

static
int sdstd_abort(sdioh_info_t *sd, uint func)
{
	int err = 0;
	int retries;

	uint16 cmd_reg;
	uint32 cmd_arg;
	uint32 rsp5;
	uint8 rflags;

	uint16 int_reg = 0;
	uint16 plain_intstatus;

	/* Argument is write to F0 (CCCR) IOAbort with function number */
	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, CMD52_FUNCTION, SDIO_FUNC_0);
	cmd_arg = SFIELD(cmd_arg, CMD52_REG_ADDR, SDIOD_CCCR_IOABORT);
	cmd_arg = SFIELD(cmd_arg, CMD52_RW_FLAG, SD_IO_OP_WRITE);
	cmd_arg = SFIELD(cmd_arg, CMD52_RAW, 0);
	cmd_arg = SFIELD(cmd_arg, CMD52_DATA, func);

	/* Command is CMD52 write */
	cmd_reg = 0;
	cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48_BUSY);
	cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 1);
	cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 1);
	cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
	cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_ABORT);
	cmd_reg = SFIELD(cmd_reg, CMD_INDEX, SDIOH_CMD_52);

	if (sd->sd_mode == SDIOH_MODE_SPI) {
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
	}

	/* Wait for CMD_INHIBIT to go away as per spec section 3.6.1.1 */
	retries = RETRIES_SMALL;
	while (GFIELD(sdstd_rreg(sd, SD_PresentState), PRES_CMD_INHIBIT)) {
		if (retries == RETRIES_SMALL)
			sd_err(("%s: Waiting for Command Inhibit, state 0x%08x\n",
			        __FUNCTION__, sdstd_rreg(sd, SD_PresentState)));
		if (!--retries) {
			sd_err(("%s: Command Inhibit timeout, state 0x%08x\n",
			        __FUNCTION__, sdstd_rreg(sd, SD_PresentState)));
			if (trap_errs)
				ASSERT(0);
			err = BCME_SDIO_ERROR;
			goto done;
		}
	}

	/* Clear errors from any previous commands */
	if ((plain_intstatus = sdstd_rreg16(sd, SD_ErrorIntrStatus)) != 0) {
		sd_err(("abort: clearing errstat 0x%04x\n", plain_intstatus));
		sdstd_wreg16(sd, SD_ErrorIntrStatus, plain_intstatus);
	}
	plain_intstatus = sdstd_rreg16(sd, SD_IntrStatus);
	if (plain_intstatus & ~(SFIELD(0, INTSTAT_CARD_INT, 1))) {
		sd_err(("abort: intstatus 0x%04x\n", plain_intstatus));
		if (GFIELD(plain_intstatus, INTSTAT_CMD_COMPLETE)) {
			sd_err(("SDSTD_ABORT: CMD COMPLETE SET BEFORE COMMAND GIVEN!!!\n"));
		}
		if (GFIELD(plain_intstatus, INTSTAT_CARD_REMOVAL)) {
			sd_err(("SDSTD_ABORT: INTSTAT_CARD_REMOVAL\n"));
			err = BCME_NODEVICE;
			goto done;
		}
	}

	/* Issue the command */
	sdstd_wreg(sd, SD_Arg0, cmd_arg);
	sdstd_wreg16(sd, SD_Command, cmd_reg);

	/* In interrupt mode return, expect later CMD_COMPLETE interrupt */
	if (!sd->polled_mode)
		return err;

	/* Otherwise, wait for the command to complete */
	retries = RETRIES_LARGE;
	do {
		int_reg = sdstd_rreg16(sd, SD_IntrStatus);
	} while (--retries &&
	         (GFIELD(int_reg, INTSTAT_ERROR_INT) == 0) &&
	         (GFIELD(int_reg, INTSTAT_CMD_COMPLETE) == 0));

	/* If command completion fails, do a cmd reset and note the error */
	if (!retries) {
		sd_err(("%s: CMD_COMPLETE timeout: intr 0x%04x err 0x%04x state 0x%08x\n",
		        __FUNCTION__, int_reg,
		        sdstd_rreg16(sd, SD_ErrorIntrStatus),
		        sdstd_rreg(sd, SD_PresentState)));

		sdstd_wreg8(sd, SD_SoftwareReset, SFIELD(0, SW_RESET_CMD, 1));
		retries = RETRIES_LARGE;
		do {
			sd_trace(("%s: waiting for CMD line reset\n", __FUNCTION__));
		} while ((GFIELD(sdstd_rreg8(sd, SD_SoftwareReset),
		                 SW_RESET_CMD)) && retries--);

		if (!retries) {
			sd_err(("%s: Timeout waiting for CMD line reset\n", __FUNCTION__));
		}

		if (trap_errs)
			ASSERT(0);

		err = BCME_SDIO_ERROR;
	}

	/* Clear Command Complete interrupt */
	int_reg = SFIELD(0, INTSTAT_CMD_COMPLETE, 1);
	sdstd_wreg16(sd, SD_IntrStatus, int_reg);

	/* Check for Errors */
	if ((plain_intstatus = sdstd_rreg16 (sd, SD_ErrorIntrStatus)) != 0) {
		sd_err(("%s: ErrorintrStatus: 0x%x, "
		        "(intrstatus = 0x%x, present state 0x%x) clearing\n",
		        __FUNCTION__, plain_intstatus,
		        sdstd_rreg16(sd, SD_IntrStatus),
		        sdstd_rreg(sd, SD_PresentState)));

		sdstd_wreg16(sd, SD_ErrorIntrStatus, plain_intstatus);

		sdstd_wreg8(sd, SD_SoftwareReset, SFIELD(0, SW_RESET_DAT, 1));
		retries = RETRIES_LARGE;
		do {
			sd_trace(("%s: waiting for DAT line reset\n", __FUNCTION__));
		} while ((GFIELD(sdstd_rreg8(sd, SD_SoftwareReset),
		                 SW_RESET_DAT)) && retries--);

		if (!retries) {
			sd_err(("%s: Timeout waiting for DAT line reset\n", __FUNCTION__));
		}

		if (trap_errs)
			ASSERT(0);

		/* ABORT is dataless, only cmd errs count */
		if (plain_intstatus & ERRINT_CMD_ERRS)
			err = BCME_SDIO_ERROR;
	}

	/* If command failed don't bother looking at response */
	if (err)
		goto done;

	/* Otherwise, check the response */
	sdstd_cmd_getrsp(sd, &rsp5, 1);
	rflags = GFIELD(rsp5, RSP5_FLAGS);

	if (rflags & SD_RSP_R5_ERRBITS) {
		sd_err(("%s: R5 flags include errbits: 0x%02x\n", __FUNCTION__, rflags));

		/* The CRC error flag applies to the previous command */
		if (rflags & (SD_RSP_R5_ERRBITS & ~SD_RSP_R5_COM_CRC_ERROR)) {
			err = BCME_SDIO_ERROR;
			goto done;
		}
	}

	if (((rflags & (SD_RSP_R5_IO_CURRENTSTATE0 | SD_RSP_R5_IO_CURRENTSTATE1)) != 0x10) &&
	    ((rflags & (SD_RSP_R5_IO_CURRENTSTATE0 | SD_RSP_R5_IO_CURRENTSTATE1)) != 0x20)) {
		sd_err(("%s: R5 flags has bad state: 0x%02x\n", __FUNCTION__, rflags));
		err = BCME_SDIO_ERROR;
		goto done;
	}

	if (GFIELD(rsp5, RSP5_STUFF)) {
		sd_err(("%s: rsp5 stuff is 0x%x: should be 0\n",
		        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));
		err = BCME_SDIO_ERROR;
		goto done;
	}

done:
	if (err == BCME_NODEVICE)
		return err;

	sdstd_wreg8(sd, SD_SoftwareReset,
	            SFIELD(SFIELD(0, SW_RESET_DAT, 1), SW_RESET_CMD, 1));

	retries = RETRIES_LARGE;
	do {
		rflags = sdstd_rreg8(sd, SD_SoftwareReset);
		if (!GFIELD(rflags, SW_RESET_DAT) && !GFIELD(rflags, SW_RESET_CMD))
			break;
	} while (--retries);

	if (!retries) {
		sd_err(("%s: Timeout waiting for DAT/CMD reset: 0x%02x\n",
		        __FUNCTION__, rflags));
		err = BCME_SDIO_ERROR;
	}

	return err;
}

extern int
sdioh_abort(sdioh_info_t *sd, uint fnum)
{
	int ret;

	sdstd_lock(sd);
	ret = sdstd_abort(sd, fnum);
	sdstd_unlock(sd);

	return ret;
}

int
sdioh_start(sdioh_info_t *sd, int stage)
{
	return SUCCESS;
}

int
sdioh_stop(sdioh_info_t *sd)
{
	return SUCCESS;
}

static int
sdstd_check_errs(sdioh_info_t *sdioh_info, uint32 cmd, uint32 arg)
{
	uint16 regval;
	uint retries;
	uint function = 0;

	/* If no errors, we're done */
	if ((regval = sdstd_rreg16(sdioh_info, SD_ErrorIntrStatus)) == 0)
		return SUCCESS;

	sd_info(("%s: ErrorIntrStatus 0x%04x (clearing), IntrStatus 0x%04x PresentState 0x%08x\n",
	        __FUNCTION__, regval, sdstd_rreg16(sdioh_info, SD_IntrStatus),
	        sdstd_rreg(sdioh_info, SD_PresentState)));
	sdstd_wreg16(sdioh_info, SD_ErrorIntrStatus, regval);

	/* On command error, issue CMD reset */
	if (regval & ERRINT_CMD_ERRS) {
		sd_trace(("%s: issuing CMD reset\n", __FUNCTION__));
		sdstd_wreg8(sdioh_info, SD_SoftwareReset, SFIELD(0, SW_RESET_CMD, 1));
		for (retries = RETRIES_LARGE; retries; retries--)
			if (!(GFIELD(sdstd_rreg8(sdioh_info, SD_SoftwareReset), SW_RESET_CMD)))
				break;
		if (!retries) {
			sd_err(("%s: Timeout waiting for CMD line reset\n", __FUNCTION__));
		}
	}

	/* On data error, issue DAT reset */
	if (regval & ERRINT_DATA_ERRS) {
		sd_trace(("%s: issuing DAT reset\n", __FUNCTION__));
		sdstd_wreg8(sdioh_info, SD_SoftwareReset, SFIELD(0, SW_RESET_DAT, 1));
		for (retries = RETRIES_LARGE; retries; retries--)
			if (!(GFIELD(sdstd_rreg8(sdioh_info, SD_SoftwareReset), SW_RESET_DAT)))
				break;
		if (!retries) {
			sd_err(("%s: Timeout waiting for DAT line reset\n", __FUNCTION__));
		}
	}

	/* For an IO command (CMD52 or CMD53) issue an abort to the appropriate function */
	if (cmd == SDIOH_CMD_53)
		function = GFIELD(arg, CMD53_FUNCTION);
	else if (cmd == SDIOH_CMD_52)
		function = GFIELD(arg, CMD52_FUNCTION);
	if (function) {
		sd_trace(("%s: requesting abort for function %d after cmd %d\n",
		          __FUNCTION__, function, cmd));
		sdstd_abort(sdioh_info, function);
	}

	if (trap_errs)
		ASSERT(0);

	return ERROR;
}



/*
 * Private/Static work routines
 */
static bool
sdstd_reset(sdioh_info_t *sd, bool host_reset, bool client_reset)
{
	int retries = RETRIES_LARGE;
	uchar regval;

	if (!sd)
		return TRUE;

	sdstd_lock(sd);
	/* Reset client card */
	if (client_reset && (sd->adapter_slot != -1)) {
		if (sdstd_card_regwrite(sd, 0, SDIOD_CCCR_IOABORT, 1, 0x8) != SUCCESS)
			sd_err(("%s: Cannot write to card reg 0x%x\n",
			        __FUNCTION__, SDIOD_CCCR_IOABORT));
		else
			sd->card_rca = 0;
	}

	/* Reset host controller */
	if (host_reset) {
		regval = SFIELD(0, SW_RESET_ALL, 1);
		sdstd_wreg8(sd, SD_SoftwareReset, regval);
		do {
			sd_trace(("%s: waiting for reset\n", __FUNCTION__));
		} while ((sdstd_rreg8(sd, SD_SoftwareReset) & regval) && retries--);

		if (!retries) {
			sd_err(("%s: Timeout waiting for host reset\n", __FUNCTION__));
			sdstd_unlock(sd);
			return (FALSE);
		}

		/* A reset should reset bus back to 1 bit mode */
		sd->sd_mode = SDIOH_MODE_SD1;
		sdstd_set_dma_mode(sd, sd->sd_dma_mode);
	}
	sdstd_unlock(sd);
	return TRUE;
}

/* Disable device interrupt */
void
sdstd_devintr_off(sdioh_info_t *sd)
{
	sd_trace(("%s: %d\n", __FUNCTION__, sd->use_client_ints));
	if (sd->use_client_ints) {
		sd->intmask &= ~CLIENT_INTR;
		sdstd_wreg16(sd, SD_IntrSignalEnable, sd->intmask);
		sdstd_rreg16(sd, SD_IntrSignalEnable); /* Sync readback */
	}
}

/* Enable device interrupt */
void
sdstd_devintr_on(sdioh_info_t *sd)
{
	ASSERT(sd->lockcount == 0);
	sd_trace(("%s: %d\n", __FUNCTION__, sd->use_client_ints));
	if (sd->use_client_ints) {
		uint16 status = sdstd_rreg16(sd, SD_IntrStatusEnable);
		sdstd_wreg16(sd, SD_IntrStatusEnable, SFIELD(status, INTSTAT_CARD_INT, 0));
		sdstd_wreg16(sd, SD_IntrStatusEnable, status);

		sd->intmask |= CLIENT_INTR;
		sdstd_wreg16(sd, SD_IntrSignalEnable, sd->intmask);
		sdstd_rreg16(sd, SD_IntrSignalEnable); /* Sync readback */
	}
}

#ifdef BCMSDYIELD
/* Enable/disable other interrupts */
void
sdstd_intrs_on(sdioh_info_t *sd, uint16 norm, uint16 err)
{
	if (err) {
		norm = SFIELD(norm, INTSTAT_ERROR_INT, 1);
		sdstd_wreg16(sd, SD_ErrorIntrSignalEnable, err);
	}

	sd->intmask |= norm;
	sdstd_wreg16(sd, SD_IntrSignalEnable, sd->intmask);
	if (sd_forcerb)
		sdstd_rreg16(sd, SD_IntrSignalEnable); /* Sync readback */
}

void
sdstd_intrs_off(sdioh_info_t *sd, uint16 norm, uint16 err)
{
	if (err) {
		norm = SFIELD(norm, INTSTAT_ERROR_INT, 1);
		sdstd_wreg16(sd, SD_ErrorIntrSignalEnable, 0);
	}

	sd->intmask &= ~norm;
	sdstd_wreg16(sd, SD_IntrSignalEnable, sd->intmask);
	if (sd_forcerb)
		sdstd_rreg16(sd, SD_IntrSignalEnable); /* Sync readback */
}
#endif /* BCMSDYIELD */

static int
sdstd_host_init(sdioh_info_t *sd)
{
	int 		num_slots, full_slot;
	uint8		reg8;

	uint32		card_ins;
	int			slot, first_bar = 0;
	bool		detect_slots = FALSE;
	uint		bar;

	/* Check for Arasan ID */
	if ((OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) & 0xFFFF) == VENDOR_SI_IMAGE) {
		sd_info(("%s: Found Arasan Standard SDIO Host Controller\n", __FUNCTION__));
		sd->controller_type = SDIOH_TYPE_ARASAN_HDK;
		detect_slots = TRUE;
	} else if ((OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) & 0xFFFF) == VENDOR_BROADCOM) {
		sd_info(("%s: Found Broadcom 27xx Standard SDIO Host Controller\n", __FUNCTION__));
		sd->controller_type = SDIOH_TYPE_BCM27XX;
		detect_slots = FALSE;
	} else if ((OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) & 0xFFFF) == VENDOR_TI) {
		sd_info(("%s: Found TI PCIxx21 Standard SDIO Host Controller\n", __FUNCTION__));
		sd->controller_type = SDIOH_TYPE_TI_PCIXX21;
		detect_slots = TRUE;
	} else if ((OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) & 0xFFFF) == VENDOR_RICOH) {
		sd_info(("%s: Ricoh Co Ltd R5C822 SD/SDIO/MMC/MS/MSPro Host Adapter\n",
			__FUNCTION__));
		sd->controller_type = SDIOH_TYPE_RICOH_R5C822;
		detect_slots = TRUE;
	} else if ((OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) & 0xFFFF) == VENDOR_JMICRON) {
		sd_info(("%s: JMicron Standard SDIO Host Controller\n",
			__FUNCTION__));
		sd->controller_type = SDIOH_TYPE_JMICRON;
		detect_slots = TRUE;
	} else {
		return ERROR;
	}

	/*
	 * Determine num of slots
	 * Search each slot
	 */

	first_bar = OSL_PCI_READ_CONFIG(sd->osh, SD_SlotInfo, 4) & 0x7;
	num_slots = (OSL_PCI_READ_CONFIG(sd->osh, SD_SlotInfo, 4) & 0xff) >> 4;
	num_slots &= 7;
	num_slots++;   	/* map bits to num slots according to spec */

	if (OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_VID, 4) ==
	    ((SDIOH_FPGA_ID << 16) | VENDOR_BROADCOM)) {
		sd_err(("%s: Found Broadcom Standard SDIO Host Controller FPGA\n", __FUNCTION__));
		/* Set BAR0 Window to SDIOSTH core */
		OSL_PCI_WRITE_CONFIG(sd->osh, PCI_BAR0_WIN, 4, 0x18001000);

		/* Set defaults particular to this controller. */
		detect_slots = TRUE;
		num_slots = 1;
		first_bar = 0;

		/* Controller supports ADMA2, so turn it on here. */
		sd->sd_dma_mode = DMA_MODE_ADMA2;
	}

	/* Map in each slot on the board and query it to see if a
	 * card is inserted.  Use the first populated slot found.
	 */
	if (sd->mem_space) {
		sdstd_reg_unmap(sd->osh, (uintptr)sd->mem_space, SDIOH_REG_WINSZ);
		sd->mem_space = NULL;
	}

	full_slot = -1;

	for (slot = 0; slot < num_slots; slot++) {
		bar = OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_BAR0 + (4*(slot + first_bar)), 4);
		sd->mem_space = (volatile char *)sdstd_reg_map(sd->osh,
		                                               (uintptr)bar, SDIOH_REG_WINSZ);

		sd->adapter_slot = -1;

		if (detect_slots) {
			card_ins = GFIELD(sdstd_rreg(sd, SD_PresentState), PRES_CARD_PRESENT);
		} else {
			card_ins = TRUE;
		}

		if (card_ins) {
			sd_info(("%s: SDIO slot %d: Full\n", __FUNCTION__, slot));
			if (full_slot < 0)
				full_slot = slot;
		} else {
			sd_info(("%s: SDIO slot %d: Empty\n", __FUNCTION__, slot));
		}

		if (sd->mem_space) {
			sdstd_reg_unmap(sd->osh, (uintptr)sd->mem_space, SDIOH_REG_WINSZ);
			sd->mem_space = NULL;
		}
	}

	if (full_slot < 0) {
		sd_err(("No slots on SDIO controller are populated\n"));
		return -1;
	}

	bar = OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_BAR0 + (4*(full_slot + first_bar)), 4);
	sd->mem_space = (volatile char *)sdstd_reg_map(sd->osh, (uintptr)bar, SDIOH_REG_WINSZ);

	sd_err(("Using slot %d at BAR%d [0x%08x] mem_space 0x%p\n",
		full_slot,
		(full_slot + first_bar),
		OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_BAR0 + (4*(full_slot + first_bar)), 4),
		sd->mem_space));


	sd->adapter_slot = full_slot;

	sd->version = sdstd_rreg16(sd, SD_HostControllerVersion) & 0xFF;
	switch (sd->version) {
		case 0:
			sd_err(("Host Controller version 1.0, Vendor Revision: 0x%02x\n",
				sdstd_rreg16(sd, SD_HostControllerVersion) >> 8));
			break;
		case 1:
		case 2:
			sd_err(("Host Controller version 2.0, Vendor Revision: 0x%02x\n",
				sdstd_rreg16(sd, SD_HostControllerVersion) >> 8));
			break;
		default:
			sd_err(("%s: Host Controller version 0x%02x not supported.\n",
			    __FUNCTION__, sd->version));
			break;
	}

	sd->caps = sdstd_rreg(sd, SD_Capabilities);	/* Cache this for later use */
	sd->curr_caps = sdstd_rreg(sd, SD_MaxCurCap);

	sdstd_set_dma_mode(sd, sd->sd_dma_mode);


	sdstd_reset(sd, 1, 0);

	/* Read SD4/SD1 mode */
	if ((reg8 = sdstd_rreg8(sd, SD_HostCntrl))) {
		if (reg8 & SD4_MODE) {
			sd_err(("%s: Host cntrlr already in 4 bit mode: 0x%x\n",
			        __FUNCTION__,  reg8));
		}
	}

	/* Default power on mode is SD1 */
	sd->sd_mode = SDIOH_MODE_SD1;
	sd->polled_mode = TRUE;
	sd->host_init_done = TRUE;
	sd->card_init_done = FALSE;
	sd->adapter_slot = full_slot;

	return (SUCCESS);
}
#define CMD5_RETRIES 200
static int
get_ocr(sdioh_info_t *sd, uint32 *cmd_arg, uint32 *cmd_rsp)
{
	int retries, status;

	/* Get the Card's Operation Condition.  Occasionally the board
	 * takes a while to become ready
	 */
	retries = CMD5_RETRIES;
	do {
		*cmd_rsp = 0;
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_5, *cmd_arg))
		    != SUCCESS) {
			sd_err(("%s: CMD5 failed\n", __FUNCTION__));
			return status;
		}
		sdstd_cmd_getrsp(sd, cmd_rsp, 1);
		if (!GFIELD(*cmd_rsp, RSP4_CARD_READY))
			sd_trace(("%s: Waiting for card to become ready\n", __FUNCTION__));
	} while ((!GFIELD(*cmd_rsp, RSP4_CARD_READY)) && --retries);
	if (!retries)
		return ERROR;

	return (SUCCESS);
}

static int
sdstd_client_init(sdioh_info_t *sd)
{
	uint32 cmd_arg, cmd_rsp;
	int status;
	uint8 fn_ints;


	sd_trace(("%s: Powering up slot %d\n", __FUNCTION__, sd->adapter_slot));

	/* Clear any pending ints */
	sdstd_wreg16(sd, SD_IntrStatus, 0x1ff);
	sdstd_wreg16(sd, SD_ErrorIntrStatus, 0x0fff);

	/* Enable both Normal and Error Status.  This does not enable
	 * interrupts, it only enables the status bits to
	 * become 'live'
	 */
	sdstd_wreg16(sd, SD_IntrStatusEnable, 0x1ff);
	sdstd_wreg16(sd, SD_ErrorIntrStatusEnable, 0xffff);

	sdstd_wreg16(sd, SD_IntrSignalEnable, 0);	  /* Disable ints for now. */

	/* Start at ~400KHz clock rate for initialization */
	if (!sdstd_start_clock(sd, 128)) {
		sd_err(("sdstd_start_clock failed\n"));
		return ERROR;
	}
	if (!sdstd_start_power(sd)) {
		sd_err(("sdstd_start_power failed\n"));
		return ERROR;
	}

	if (sd->num_funcs == 0) {
		sd_err(("%s: No IO funcs!\n", __FUNCTION__));
		return ERROR;
	}

	/* In SPI mode, issue CMD0 first */
	if (sd->sd_mode == SDIOH_MODE_SPI) {
		cmd_arg = 0;
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_0, cmd_arg))
		    != SUCCESS) {
			sd_err(("BCMSDIOH: cardinit: CMD0 failed!\n"));
			return status;
		}
	}

	if (sd->sd_mode != SDIOH_MODE_SPI) {
		uint16 rsp6_status;

		/* Card is operational. Ask it to send an RCA */
		cmd_arg = 0;
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_3, cmd_arg))
		    != SUCCESS) {
			sd_err(("%s: CMD3 failed!\n", __FUNCTION__));
			return status;
		}

		/* Verify the card status returned with the cmd response */
		sdstd_cmd_getrsp(sd, &cmd_rsp, 1);
		rsp6_status = GFIELD(cmd_rsp, RSP6_STATUS);
		if (GFIELD(rsp6_status, RSP6STAT_COM_CRC_ERROR) ||
		    GFIELD(rsp6_status, RSP6STAT_ILLEGAL_CMD) ||
		    GFIELD(rsp6_status, RSP6STAT_ERROR)) {
			sd_err(("%s: CMD3 response error. Response = 0x%x!\n",
			        __FUNCTION__, rsp6_status));
			return ERROR;
		}

		/* Save the Card's RCA */
		sd->card_rca = GFIELD(cmd_rsp, RSP6_IO_RCA);
		sd_info(("RCA is 0x%x\n", sd->card_rca));

		if (rsp6_status)
			sd_err(("raw status is 0x%x\n", rsp6_status));

		/* Select the card */
		cmd_arg = SFIELD(0, CMD7_RCA, sd->card_rca);
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_7, cmd_arg))
		    != SUCCESS) {
			sd_err(("%s: CMD7 failed!\n", __FUNCTION__));
			return status;
		}
		sdstd_cmd_getrsp(sd, &cmd_rsp, 1);
		if (cmd_rsp != SDIOH_CMD7_EXP_STATUS) {
			sd_err(("%s: CMD7 response error. Response = 0x%x!\n",
			        __FUNCTION__, cmd_rsp));
			return ERROR;
		}
	}

	sdstd_card_enablefuncs(sd);

	if (!sdstd_bus_width(sd, sd_sdmode)) {
		sd_err(("sdstd_bus_width failed\n"));
		return ERROR;
	}

	set_client_block_size(sd, 1, BLOCK_SIZE_4318);
	fn_ints = INTR_CTL_FUNC1_EN;

	if (sd->num_funcs >= 2) {
		set_client_block_size(sd, 2, sd_f2_blocksize /* BLOCK_SIZE_4328 */);
		fn_ints |= INTR_CTL_FUNC2_EN;
	}

	/* Enable/Disable Client interrupts */
	/* Turn on here but disable at host controller? */
	if (sdstd_card_regwrite(sd, 0, SDIOD_CCCR_INTEN, 1,
	                        (fn_ints | INTR_CTL_MASTER_EN)) != SUCCESS) {
		sd_err(("%s: Could not enable ints in CCCR\n", __FUNCTION__));
		return ERROR;
	}

	/* Switch to High-speed clocking mode if both host and device support it */
	sdstd_set_highspeed_mode(sd, (bool)sd_hiok);

	/* After configuring for High-Speed mode, set the desired clock rate. */
	if (!sdstd_start_clock(sd, (uint16)sd_divisor)) {
		sd_err(("sdstd_start_clock failed\n"));
		return ERROR;
	}

	sd->card_init_done = TRUE;

	return SUCCESS;
}

static int
sdstd_set_highspeed_mode(sdioh_info_t *sd, bool HSMode)
{
	uint32 regdata;
	int status;
	uint8 reg8;

	reg8 = sdstd_rreg8(sd, SD_HostCntrl);


	if (HSMode == TRUE) {
		if (sd_hiok && (GFIELD(sd->caps, CAP_HIGHSPEED)) == 0) {
			sd_err(("Host Controller does not support hi-speed mode.\n"));
			return BCME_ERROR;
		}

		sd_info(("Attempting to enable High-Speed mode.\n"));

		if ((status = sdstd_card_regread(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
		                                 1, &regdata)) != SUCCESS) {
			return BCME_SDIO_ERROR;
		}
		if (regdata & SDIO_SPEED_SHS) {
			sd_info(("Device supports High-Speed mode.\n"));

			regdata |= SDIO_SPEED_EHS;

			sd_info(("Writing %08x to Card at %08x\n",
			         regdata, SDIOD_CCCR_SPEED_CONTROL));
			if ((status = sdstd_card_regwrite(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
			                                  1, regdata)) != BCME_OK) {
				return BCME_SDIO_ERROR;
			}

			if ((status = sdstd_card_regread(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
			                                 1, &regdata)) != BCME_OK) {
				return BCME_SDIO_ERROR;
			}

			sd_info(("Read %08x to Card at %08x\n", regdata, SDIOD_CCCR_SPEED_CONTROL));

			reg8 = SFIELD(reg8, HOST_HI_SPEED_EN, 1);

			sd_err(("High-speed clocking mode enabled.\n"));
		}
		else {
			sd_err(("Device does not support High-Speed Mode.\n"));
			reg8 = SFIELD(reg8, HOST_HI_SPEED_EN, 0);
		}
	} else {
		/* Force off device bit */
		if ((status = sdstd_card_regread(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
		                                 1, &regdata)) != BCME_OK) {
			return status;
		}
		if (regdata & SDIO_SPEED_EHS) {
			regdata &= ~SDIO_SPEED_EHS;
			if ((status = sdstd_card_regwrite(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
			                                  1, regdata)) != BCME_OK) {
				return status;
			}
		}

		sd_err(("High-speed clocking mode disabled.\n"));
		reg8 = SFIELD(reg8, HOST_HI_SPEED_EN, 0);
	}

	sdstd_wreg8(sd, SD_HostCntrl, reg8);

	return BCME_OK;
}

/* Select DMA Mode:
 * If dma_mode == DMA_MODE_AUTO, pick the "best" mode.
 * Otherwise, pick the selected mode if supported.
 * If not supported, use PIO mode.
 */
static int
sdstd_set_dma_mode(sdioh_info_t *sd, int8 dma_mode)
{
	uint8 reg8, dma_sel_bits = SDIOH_SDMA_MODE;
	int8 prev_dma_mode = sd->sd_dma_mode;

	switch (prev_dma_mode) {
		case DMA_MODE_AUTO:
			sd_dma(("%s: Selecting best DMA mode supported by controller.\n",
			          __FUNCTION__));
			if (GFIELD(sd->caps, CAP_ADMA2)) {
				sd->sd_dma_mode = DMA_MODE_ADMA2;
				dma_sel_bits = SDIOH_ADMA2_MODE;
			} else if (GFIELD(sd->caps, CAP_ADMA1)) {
				sd->sd_dma_mode = DMA_MODE_ADMA1;
				dma_sel_bits = SDIOH_ADMA1_MODE;
			} else if (GFIELD(sd->caps, CAP_DMA)) {
				sd->sd_dma_mode = DMA_MODE_SDMA;
			} else {
				sd->sd_dma_mode = DMA_MODE_NONE;
			}
			break;
		case DMA_MODE_NONE:
			sd->sd_dma_mode = DMA_MODE_NONE;
			break;
		case DMA_MODE_SDMA:
			if (GFIELD(sd->caps, CAP_DMA)) {
				sd->sd_dma_mode = DMA_MODE_SDMA;
			} else {
				sd_err(("%s: SDMA not supported by controller.\n", __FUNCTION__));
				sd->sd_dma_mode = DMA_MODE_NONE;
			}
			break;
		case DMA_MODE_ADMA1:
			if (GFIELD(sd->caps, CAP_ADMA1)) {
				sd->sd_dma_mode = DMA_MODE_ADMA1;
				dma_sel_bits = SDIOH_ADMA1_MODE;
			} else {
				sd_err(("%s: ADMA1 not supported by controller.\n", __FUNCTION__));
				sd->sd_dma_mode = DMA_MODE_NONE;
			}
			break;
		case DMA_MODE_ADMA2:
			if (GFIELD(sd->caps, CAP_ADMA2)) {
				sd->sd_dma_mode = DMA_MODE_ADMA2;
				dma_sel_bits = SDIOH_ADMA2_MODE;
			} else {
				sd_err(("%s: ADMA2 not supported by controller.\n", __FUNCTION__));
				sd->sd_dma_mode = DMA_MODE_NONE;
			}
			break;
		case DMA_MODE_ADMA2_64:
			sd_err(("%s: 64b ADMA2 not supported by driver.\n", __FUNCTION__));
			sd->sd_dma_mode = DMA_MODE_NONE;
			break;
		default:
			sd_err(("%s: Unsupported DMA Mode %d requested.\n", __FUNCTION__,
			        prev_dma_mode));
			sd->sd_dma_mode = DMA_MODE_NONE;
			break;
	}

	/* clear SysAddr, only used for SDMA */
	sdstd_wreg(sd, SD_SysAddr, 0);

	sd_err(("%s: %s mode selected.\n", __FUNCTION__, dma_mode_description[sd->sd_dma_mode]));

	reg8 = sdstd_rreg8(sd, SD_HostCntrl);
	reg8 = SFIELD(reg8, HOST_DMA_SEL, dma_sel_bits);
	sdstd_wreg8(sd, SD_HostCntrl, reg8);
	sd_dma(("%s: SD_HostCntrl=0x%02x\n", __FUNCTION__, reg8));

	return BCME_OK;
}


bool
sdstd_start_clock(sdioh_info_t *sd, uint16 new_sd_divisor)
{
	uint rc, count;
	uint16 divisor;

	/* turn off HC clock */
	sdstd_wreg16(sd, SD_ClockCntrl,
	             sdstd_rreg16(sd, SD_ClockCntrl) & ~((uint16)0x4)); /*  Disable the HC clock */

	/* Set divisor */

	divisor = (new_sd_divisor >> 1) << 8;

	sd_info(("Clock control is 0x%x\n", sdstd_rreg16(sd, SD_ClockCntrl)));
	sdstd_mod_reg16(sd, SD_ClockCntrl, 0xff00, divisor);
	sd_info(("%s: Using clock divisor of %d (regval 0x%04x)\n", __FUNCTION__,
	         new_sd_divisor, divisor));

	sd_info(("Primary Clock Freq = %d MHz\n", GFIELD(sd->caps, CAP_TO_CLKFREQ)));

	if (GFIELD(sd->caps, CAP_TO_CLKFREQ) == 50) {
		sd_info(("%s: Resulting SDIO clock is %d %s\n", __FUNCTION__,
		        ((50 % new_sd_divisor) ? (50000 / new_sd_divisor) : (50 / new_sd_divisor)),
		        ((50 % new_sd_divisor) ? "KHz" : "MHz")));
	} else if (GFIELD(sd->caps, CAP_TO_CLKFREQ) == 48) {
		sd_info(("%s: Resulting SDIO clock is %d %s\n", __FUNCTION__,
		        ((48 % new_sd_divisor) ? (48000 / new_sd_divisor) : (48 / new_sd_divisor)),
		        ((48 % new_sd_divisor) ? "KHz" : "MHz")));
	} else if (GFIELD(sd->caps, CAP_TO_CLKFREQ) == 33) {
		sd_info(("%s: Resulting SDIO clock is %d %s\n", __FUNCTION__,
		        ((33 % new_sd_divisor) ? (33000 / new_sd_divisor) : (33 / new_sd_divisor)),
		        ((33 % new_sd_divisor) ? "KHz" : "MHz")));

	} else if (sd->controller_type == SDIOH_TYPE_BCM27XX) {
	} else {
		sd_err(("Need to determine divisor for %d MHz clocks\n",
		        GFIELD(sd->caps, CAP_TO_CLKFREQ)));
		sd_err(("Consult SD Host Controller Spec: Clock Control Register\n"));
		return (FALSE);
	}

	sdstd_or_reg16(sd, SD_ClockCntrl, 0x1); /*  Enable the clock */

	/* Wait for clock to stabilize */
	rc = (sdstd_rreg16(sd, SD_ClockCntrl) & 2);
	count = 0;
	while (!rc) {
		OSL_DELAY(1);
		sd_info(("Waiting for clock to become stable 0x%x\n", rc));
		rc = (sdstd_rreg16(sd, SD_ClockCntrl) & 2);
		count++;
		if (count > 10000) {
			sd_err(("%s:Clocks failed to stabilize after %u attempts",
			        __FUNCTION__, count));
			return (FALSE);
		}
	}
	/* Turn on clock */
	sdstd_or_reg16(sd, SD_ClockCntrl, 0x4);

	/* Set timeout control (adjust default value based on divisor).
	 * Disabling timeout interrupts during setting is advised by host spec.
	 */
	{
		uint16 regdata;
		uint toval;

		toval = sd_toctl;
		divisor = new_sd_divisor;

		while (toval && !(divisor & 1)) {
			toval -= 1;
			divisor >>= 1;
		}

		regdata = sdstd_rreg16(sd, SD_ErrorIntrStatusEnable);
		sdstd_wreg16(sd, SD_ErrorIntrStatusEnable, (regdata & ~ERRINT_DATA_TIMEOUT_BIT));
		sdstd_wreg8(sd, SD_TimeoutCntrl, (uint8)toval);
		sdstd_wreg16(sd, SD_ErrorIntrStatusEnable, regdata);
	}

	OSL_DELAY(2);

	sd_info(("Final Clock control is 0x%x\n", sdstd_rreg16(sd, SD_ClockCntrl)));

	return TRUE;
}

bool
sdstd_start_power(sdioh_info_t *sd)
{
	char *s;
	uint32 cmd_arg;
	uint32 cmd_rsp;
	uint8 pwr = 0;
	int volts;

	volts = 0;
	s = NULL;
	if (GFIELD(sd->caps, CAP_VOLT_1_8)) {
		volts = 5;
		s = "1.8";
	}
	if (GFIELD(sd->caps, CAP_VOLT_3_0)) {
		volts = 6;
		s = "3.0";
	}
	if (GFIELD(sd->caps, CAP_VOLT_3_3)) {
		volts = 7;
		s = "3.3";
	}

	pwr = SFIELD(pwr, PWR_VOLTS, volts);
	pwr = SFIELD(pwr, PWR_BUS_EN, 1);
	sdstd_wreg8(sd, SD_PwrCntrl, pwr); /* Set Voltage level */
	sd_info(("Setting Bus Power to %s Volts\n", s));

	/* Wait for power to stabilize, Dongle takes longer than NIC. */
	OSL_DELAY(250000);

	/* Get the Card's Operation Condition.  Occasionally the board
	 * takes a while to become ready
	 */
	cmd_arg = 0;
	cmd_rsp = 0;
	if (get_ocr(sd, &cmd_arg, &cmd_rsp) != SUCCESS) {
		sd_err(("%s: Failed to get OCR bailing\n", __FUNCTION__));
		sdstd_reset(sd, 0, 1);
		return FALSE;
	}

	sd_info(("mem_present = %d\n", GFIELD(cmd_rsp, RSP4_MEM_PRESENT)));
	sd_info(("num_funcs = %d\n", GFIELD(cmd_rsp, RSP4_NUM_FUNCS)));
	sd_info(("card_ready = %d\n", GFIELD(cmd_rsp, RSP4_CARD_READY)));
	sd_info(("OCR = 0x%x\n", GFIELD(cmd_rsp, RSP4_IO_OCR)));

	/* Verify that the card supports I/O mode */
	if (GFIELD(cmd_rsp, RSP4_NUM_FUNCS) == 0) {
		sd_err(("%s: Card does not support I/O\n", __FUNCTION__));
		return ERROR;
	}
	sd->num_funcs = GFIELD(cmd_rsp, RSP4_NUM_FUNCS);

	/* Examine voltage: Arasan only supports 3.3 volts,
	 * so look for 3.2-3.3 Volts and also 3.3-3.4 volts.
	 */

	if ((GFIELD(cmd_rsp, RSP4_IO_OCR) & (0x3 << 20)) == 0) {
		sd_err(("This client does not support 3.3 volts!\n"));
		return ERROR;
	}
	sd_info(("Leaving bus power at 3.3 Volts\n"));

	cmd_arg = SFIELD(0, CMD5_OCR, 0xfff000);
	cmd_rsp = 0;
	get_ocr(sd, &cmd_arg, &cmd_rsp);
	sd_info(("OCR = 0x%x\n", GFIELD(cmd_rsp, RSP4_IO_OCR)));
	return TRUE;
}

bool
sdstd_bus_width(sdioh_info_t *sd, int new_mode)
{
	uint32 regdata;
	int status;
	uint8 reg8;

	sd_trace(("%s\n", __FUNCTION__));
	if (sd->sd_mode == new_mode) {
		sd_info(("%s: Already at width %d\n", __FUNCTION__, new_mode));
		/* Could exit, but continue just in case... */
	}

	/* Set client side via reg 0x7 in CCCR */
	if ((status = sdstd_card_regread (sd, 0, SDIOD_CCCR_BICTRL, 1, &regdata)) != SUCCESS)
		return (bool)status;
	regdata &= ~BUS_SD_DATA_WIDTH_MASK;
	if (new_mode == SDIOH_MODE_SD4) {
		sd_info(("Changing to SD4 Mode\n"));
		regdata |= SD4_MODE;
	} else if (new_mode == SDIOH_MODE_SD1) {
		sd_info(("Changing to SD1 Mode\n"));
	} else {
		sd_err(("SPI Mode not supported by Standard Host Controller\n"));
	}

	if ((status = sdstd_card_regwrite (sd, 0, SDIOD_CCCR_BICTRL, 1, regdata)) != SUCCESS)
		return (bool)status;

	/* Set host side via Host reg */
	reg8 = sdstd_rreg8(sd, SD_HostCntrl) & ~SD4_MODE;
	if (new_mode == SDIOH_MODE_SD4)
		reg8 |= SD4_MODE;
	sdstd_wreg8(sd, SD_HostCntrl, reg8);

	sd->sd_mode = new_mode;

	return TRUE;
}

static int
sdstd_driver_init(sdioh_info_t *sd)
{
	sd_trace(("%s\n", __FUNCTION__));
	if ((sdstd_host_init(sd)) != SUCCESS) {
		return ERROR;
	}

	if (sdstd_client_init(sd) != SUCCESS) {
		return ERROR;
	}

	return SUCCESS;
}

static int
sdstd_get_cisaddr(sdioh_info_t *sd, uint32 regaddr)
{
	/* read 24 bits and return valid 17 bit addr */
	int i;
	uint32 scratch, regdata;
	uint8 *ptr = (uint8 *)&scratch;
	for (i = 0; i < 3; i++) {
		if ((sdstd_card_regread (sd, 0, regaddr, 1, &regdata)) != SUCCESS)
			sd_err(("%s: Can't read!\n", __FUNCTION__));

		*ptr++ = (uint8) regdata;
		regaddr++;
	}
	/* Only the lower 17-bits are valid */
	scratch = ltoh32(scratch);
	scratch &= 0x0001FFFF;
	return (scratch);
}

static int
sdstd_card_enablefuncs(sdioh_info_t *sd)
{
	int status;
	uint32 regdata;
	uint32 fbraddr;
	uint8 func;

	sd_trace(("%s\n", __FUNCTION__));

	/* Get the Card's common CIS address */
	sd->com_cis_ptr = sdstd_get_cisaddr(sd, SDIOD_CCCR_CISPTR_0);
	sd->func_cis_ptr[0] = sd->com_cis_ptr;
	sd_info(("%s: Card's Common CIS Ptr = 0x%x\n", __FUNCTION__, sd->com_cis_ptr));

	/* Get the Card's function CIS (for each function) */
	for (fbraddr = SDIOD_FBR_STARTADDR, func = 1;
	     func <= sd->num_funcs; func++, fbraddr += SDIOD_FBR_SIZE) {
		sd->func_cis_ptr[func] = sdstd_get_cisaddr(sd, SDIOD_FBR_CISPTR_0 + fbraddr);
		sd_info(("%s: Function %d CIS Ptr = 0x%x\n",
		         __FUNCTION__, func, sd->func_cis_ptr[func]));
	}

	/* Enable function 1 on the card */
	regdata = SDIO_FUNC_ENABLE_1;
	if ((status = sdstd_card_regwrite(sd, 0, SDIOD_CCCR_IOEN, 1, regdata)) != SUCCESS)
		return status;

	return SUCCESS;
}

/* Read client card reg */
static int
sdstd_card_regread(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 *data)
{
	int status;
	uint32 cmd_arg;
	uint32 rsp5;


	cmd_arg = 0;

	if ((func == 0) || (regsize == 1)) {
		cmd_arg = SFIELD(cmd_arg, CMD52_FUNCTION, func);
		cmd_arg = SFIELD(cmd_arg, CMD52_REG_ADDR, regaddr);
		cmd_arg = SFIELD(cmd_arg, CMD52_RW_FLAG, SDIOH_XFER_TYPE_READ);
		cmd_arg = SFIELD(cmd_arg, CMD52_RAW, 0);
		cmd_arg = SFIELD(cmd_arg, CMD52_DATA, 0);

		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_52, cmd_arg))
		    != SUCCESS)
			return status;

		sdstd_cmd_getrsp(sd, &rsp5, 1);
		if (sdstd_rreg16(sd, SD_ErrorIntrStatus) != 0) {
			sd_err(("%s: 1: ErrorintrStatus 0x%x\n",
			        __FUNCTION__, sdstd_rreg16(sd, SD_ErrorIntrStatus)));
		}

		if (GFIELD(rsp5, RSP5_FLAGS) != 0x10)
			sd_err(("%s: rsp5 flags is 0x%x\t %d\n",
			        __FUNCTION__, GFIELD(rsp5, RSP5_FLAGS), func));

		if (GFIELD(rsp5, RSP5_STUFF))
			sd_err(("%s: rsp5 stuff is 0x%x: should be 0\n",
			        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));
		*data = GFIELD(rsp5, RSP5_DATA);
	} else {
		cmd_arg = SFIELD(cmd_arg, CMD53_BYTE_BLK_CNT, regsize);
		cmd_arg = SFIELD(cmd_arg, CMD53_OP_CODE, 1);
		cmd_arg = SFIELD(cmd_arg, CMD53_BLK_MODE, 0);
		cmd_arg = SFIELD(cmd_arg, CMD53_FUNCTION, func);
		cmd_arg = SFIELD(cmd_arg, CMD53_REG_ADDR, regaddr);
		cmd_arg = SFIELD(cmd_arg, CMD53_RW_FLAG, SDIOH_XFER_TYPE_READ);

		sd->data_xfer_count = regsize;

		/* sdstd_cmd_issue() returns with the command complete bit
		 * in the ISR already cleared
		 */
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_53, cmd_arg))
		    != SUCCESS)
			return status;

		sdstd_cmd_getrsp(sd, &rsp5, 1);

		if (GFIELD(rsp5, RSP5_FLAGS) != 0x10)
			sd_err(("%s: rsp5 flags is 0x%x\t %d\n",
			        __FUNCTION__, GFIELD(rsp5, RSP5_FLAGS), func));

		if (GFIELD(rsp5, RSP5_STUFF))
			sd_err(("%s: rsp5 stuff is 0x%x: should be 0\n",
			        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));

		if (sd->polled_mode) {
			volatile uint16 int_reg;
			int retries = RETRIES_LARGE;

			/* Wait for Read Buffer to become ready */
			do {
				int_reg = sdstd_rreg16(sd, SD_IntrStatus);
			} while (--retries && (GFIELD(int_reg, INTSTAT_BUF_READ_READY) == 0));

			if (!retries) {
				sd_err(("%s: Timeout on Buf_Read_Ready: "
				        "intStat: 0x%x errint: 0x%x PresentState 0x%x\n",
				        __FUNCTION__, int_reg,
				        sdstd_rreg16(sd, SD_ErrorIntrStatus),
				        sdstd_rreg(sd, SD_PresentState)));
				sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg);
				return (ERROR);
			}

			/* Have Buffer Ready, so clear it and read the data */
			sdstd_wreg16(sd, SD_IntrStatus, SFIELD(0, INTSTAT_BUF_READ_READY, 1));
			if (regsize == 2)
				*data = sdstd_rreg16(sd, SD_BufferDataPort0);
			else
				*data = sdstd_rreg(sd, SD_BufferDataPort0);

			/* Check Status.
			 * After the data is read, the Transfer Complete bit should be on
			 */
			retries = RETRIES_LARGE;
			do {
				int_reg = sdstd_rreg16(sd, SD_IntrStatus);
			} while (--retries && (GFIELD(int_reg, INTSTAT_XFER_COMPLETE) == 0));

			/* Check for any errors from the data phase */
			if (sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg))
				return ERROR;

			if (!retries) {
				sd_err(("%s: Timeout on xfer complete: "
				        "intr 0x%04x err 0x%04x state 0x%08x\n",
				        __FUNCTION__, int_reg,
				        sdstd_rreg16(sd, SD_ErrorIntrStatus),
				        sdstd_rreg(sd, SD_PresentState)));
				return (ERROR);
			}

			sdstd_wreg16(sd, SD_IntrStatus, SFIELD(0, INTSTAT_XFER_COMPLETE, 1));
		}
	}
	if (sd->polled_mode) {
		if (regsize == 2)
			*data &= 0xffff;
	}
	return SUCCESS;
}

bool
check_client_intr(sdioh_info_t *sd)
{
	uint16 raw_int, cur_int, old_int;

	raw_int = sdstd_rreg16(sd, SD_IntrStatus);
	cur_int = raw_int & sd->intmask;

	if (!cur_int) {
		/* Not an error -- might share interrupts... */
		return FALSE;
	}

	if (GFIELD(cur_int, INTSTAT_CARD_INT)) {
		old_int = sdstd_rreg16(sd, SD_IntrStatusEnable);
		sdstd_wreg16(sd, SD_IntrStatusEnable, SFIELD(old_int, INTSTAT_CARD_INT, 0));

		if (sd->client_intr_enabled && sd->use_client_ints) {
			sd->intrcount++;
			ASSERT(sd->intr_handler);
			ASSERT(sd->intr_handler_arg);
			(sd->intr_handler)(sd->intr_handler_arg);
		} else {
			sd_err(("%s: Not ready for intr: enabled %d, handler %p\n",
			        __FUNCTION__, sd->client_intr_enabled, sd->intr_handler));
		}
		sdstd_wreg16(sd, SD_IntrStatusEnable, old_int);
	} else {
		/* Local interrupt: disable, set flag, and save intrstatus */
		sdstd_wreg16(sd, SD_IntrSignalEnable, 0);
		sdstd_wreg16(sd, SD_ErrorIntrSignalEnable, 0);
		sd->local_intrcount++;
		sd->got_hcint = TRUE;
		sd->last_intrstatus = cur_int;
	}

	return TRUE;
}

void
sdstd_spinbits(sdioh_info_t *sd, uint16 norm, uint16 err)
{
	uint16 int_reg, err_reg;
	int retries = RETRIES_LARGE;

	do {
		int_reg = sdstd_rreg16(sd, SD_IntrStatus);
		err_reg = sdstd_rreg16(sd, SD_ErrorIntrStatus);
	} while (--retries && !(int_reg & norm) && !(err_reg & err));

	norm |= sd->intmask;
	if (err_reg & err)
		norm = SFIELD(norm, INTSTAT_ERROR_INT, 1);
	sd->last_intrstatus = int_reg & norm;
}

/* write a client register */
static int
sdstd_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 data)
{
	int status;
	uint32 cmd_arg, rsp5, flags;

	cmd_arg = 0;

	if ((func == 0) || (regsize == 1)) {
		cmd_arg = SFIELD(cmd_arg, CMD52_FUNCTION, func);
		cmd_arg = SFIELD(cmd_arg, CMD52_REG_ADDR, regaddr);
		cmd_arg = SFIELD(cmd_arg, CMD52_RW_FLAG, SDIOH_XFER_TYPE_WRITE);
		cmd_arg = SFIELD(cmd_arg, CMD52_RAW, 0);
		cmd_arg = SFIELD(cmd_arg, CMD52_DATA, data & 0xff);
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_52, cmd_arg))
		    != SUCCESS)
			return status;

		sdstd_cmd_getrsp(sd, &rsp5, 1);
		flags = GFIELD(rsp5, RSP5_FLAGS);
		if (flags && (flags != 0x10))
			sd_err(("%s: rsp5.rsp5.flags = 0x%x, expecting 0x10\n",
			        __FUNCTION__,  flags));
	}
	else {
		cmd_arg = SFIELD(cmd_arg, CMD53_BYTE_BLK_CNT, regsize);
		cmd_arg = SFIELD(cmd_arg, CMD53_OP_CODE, 1);
		cmd_arg = SFIELD(cmd_arg, CMD53_BLK_MODE, 0);
		cmd_arg = SFIELD(cmd_arg, CMD53_FUNCTION, func);
		cmd_arg = SFIELD(cmd_arg, CMD53_REG_ADDR, regaddr);
		cmd_arg = SFIELD(cmd_arg, CMD53_RW_FLAG, SDIOH_XFER_TYPE_WRITE);

		sd->data_xfer_count = regsize;

		/* sdstd_cmd_issue() returns with the command complete bit
		 * in the ISR already cleared
		 */
		if ((status = sdstd_cmd_issue(sd, USE_DMA(sd), SDIOH_CMD_53, cmd_arg))
		    != SUCCESS)
			return status;

		sdstd_cmd_getrsp(sd, &rsp5, 1);

		if (GFIELD(rsp5, RSP5_FLAGS) != 0x10)
			sd_err(("%s: rsp5 flags = 0x%x, expecting 0x10\n",
			        __FUNCTION__,  GFIELD(rsp5, RSP5_FLAGS)));
		if (GFIELD(rsp5, RSP5_STUFF))
			sd_err(("%s: rsp5 stuff is 0x%x: expecting 0\n",
			        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));

		if (sd->polled_mode) {
			uint16 int_reg;
			int retries = RETRIES_LARGE;

			/* Wait for Write Buffer to become ready */
			do {
				int_reg = sdstd_rreg16(sd, SD_IntrStatus);
			} while (--retries && (GFIELD(int_reg, INTSTAT_BUF_WRITE_READY) == 0));

			if (!retries) {
				sd_err(("%s: Timeout on Buf_Write_Ready: intStat: 0x%x "
				        "errint: 0x%x PresentState 0x%x\n",
				        __FUNCTION__, int_reg,
				        sdstd_rreg16(sd, SD_ErrorIntrStatus),
				        sdstd_rreg(sd, SD_PresentState)));
				sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg);
				return (ERROR);
			}
			/* Clear Write Buf Ready bit */
			int_reg = 0;
			int_reg = SFIELD(int_reg, INTSTAT_BUF_WRITE_READY, 1);
			sdstd_wreg16(sd, SD_IntrStatus, int_reg);

			/* At this point we have Buffer Ready, so write the data */
			if (regsize == 2)
				sdstd_wreg16(sd, SD_BufferDataPort0, (uint16) data);
			else
				sdstd_wreg(sd, SD_BufferDataPort0, data);

			/* Wait for Transfer Complete */
			retries = RETRIES_LARGE;
			do {
				int_reg = sdstd_rreg16(sd, SD_IntrStatus);
			} while (--retries && (GFIELD(int_reg, INTSTAT_XFER_COMPLETE) == 0));

			/* Check for any errors from the data phase */
			if (sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg))
				return ERROR;

			if (retries == 0) {
				sd_err(("%s: Timeout for xfer complete; State = 0x%x, "
				        "intr state=0x%x, Errintstatus 0x%x rcnt %d, tcnt %d\n",
				        __FUNCTION__, sdstd_rreg(sd, SD_PresentState),
				        int_reg, sdstd_rreg16(sd, SD_ErrorIntrStatus),
				        sd->r_cnt, sd->t_cnt));
			}
			/* Clear the status bits */
			sdstd_wreg16(sd, SD_IntrStatus, SFIELD(int_reg, INTSTAT_CARD_INT, 0));
		}
	}
	return SUCCESS;
}

void
sdstd_cmd_getrsp(sdioh_info_t *sd, uint32 *rsp_buffer, int count /* num 32 bit words */)
{
	int rsp_count;
	int respaddr = SD_Response0;

	if (count > 4)
		count = 4;

	for (rsp_count = 0; rsp_count < count; rsp_count++) {
		*rsp_buffer++ = sdstd_rreg(sd, respaddr);
		respaddr += 4;
	}
}

static int
sdstd_cmd_issue(sdioh_info_t *sdioh_info, bool use_dma, uint32 cmd, uint32 arg)
{
	uint16 cmd_reg;
	int retries;
	uint32 cmd_arg;
	uint16 xfer_reg = 0;


	if ((sdioh_info->sd_mode == SDIOH_MODE_SPI) &&
	    ((cmd == SDIOH_CMD_3) || (cmd == SDIOH_CMD_7) || (cmd == SDIOH_CMD_15))) {
		sd_err(("%s: Cmd %d is not for SPI\n", __FUNCTION__, cmd));
		return ERROR;
	}

	retries = RETRIES_SMALL;
	while ((GFIELD(sdstd_rreg(sdioh_info, SD_PresentState), PRES_CMD_INHIBIT)) && --retries) {
		if (retries == RETRIES_SMALL)
			sd_err(("%s: Waiting for Command Inhibit cmd = %d 0x%x\n",
			        __FUNCTION__, cmd, sdstd_rreg(sdioh_info, SD_PresentState)));
	}
	if (!retries) {
		sd_err(("%s: Command Inhibit timeout\n", __FUNCTION__));
		if (trap_errs)
			ASSERT(0);
		return ERROR;
	}


	cmd_reg = 0;
	switch (cmd) {
	case SDIOH_CMD_0:       /* Set Card to Idle State - No Response */
		sd_data(("%s: CMD%d\n", __FUNCTION__, cmd));
		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_NONE);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_3:	/* Ask card to send RCA - Response R6 */
		sd_data(("%s: CMD%d\n", __FUNCTION__, cmd));
		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_5:	/* Send Operation condition - Response R4 */
		sd_data(("%s: CMD%d\n", __FUNCTION__, cmd));
		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_7:	/* Select card - Response R1 */
		sd_data(("%s: CMD%d\n", __FUNCTION__, cmd));
		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_15:	/* Set card to inactive state - Response None */
		sd_data(("%s: CMD%d\n", __FUNCTION__, cmd));
		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_NONE);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_52:	/* IO R/W Direct (single byte) - Response R5 */

		sd_data(("%s: CMD52 func(%d) addr(0x%x) %s data(0x%x)\n",
			__FUNCTION__,
			GFIELD(arg, CMD52_FUNCTION),
			GFIELD(arg, CMD52_REG_ADDR),
			GFIELD(arg, CMD52_RW_FLAG) ? "W" : "R",
			GFIELD(arg, CMD52_DATA)));

		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);
		break;

	case SDIOH_CMD_53:	/* IO R/W Extended (multiple bytes/blocks) */

		sd_data(("%s: CMD53 func(%d) addr(0x%x) %s mode(%s) cnt(%d), %s\n",
			__FUNCTION__,
			GFIELD(arg, CMD53_FUNCTION),
			GFIELD(arg, CMD53_REG_ADDR),
			GFIELD(arg, CMD53_RW_FLAG) ? "W" : "R",
			GFIELD(arg, CMD53_BLK_MODE) ? "Block" : "Byte",
			GFIELD(arg, CMD53_BYTE_BLK_CNT),
			GFIELD(arg, CMD53_OP_CODE) ? "Incrementing addr" : "Single addr"));

		cmd_arg = arg;
		xfer_reg = 0;

		cmd_reg = SFIELD(cmd_reg, CMD_RESP_TYPE, RESP_TYPE_48);
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_DATA_EN, 1);
		cmd_reg = SFIELD(cmd_reg, CMD_TYPE, CMD_TYPE_NORMAL);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX, cmd);

		use_dma = USE_DMA(sdioh_info) && GFIELD(cmd_arg, CMD53_BLK_MODE);

		if (GFIELD(cmd_arg, CMD53_BLK_MODE)) {
			uint16 blocksize;
			uint16 blockcount;
			int func;

			ASSERT(sdioh_info->sd_blockmode);

			func = GFIELD(cmd_arg, CMD53_FUNCTION);
			blocksize = MIN((int)sdioh_info->data_xfer_count,
			                sdioh_info->client_block_size[func]);
			blockcount = GFIELD(cmd_arg, CMD53_BYTE_BLK_CNT);

			/* data_xfer_cnt is already setup so that for multiblock mode,
			 * it is the entire buffer length.  For non-block or single block,
			 * it is < 64 bytes
			 */
			if (use_dma) {
				switch (sdioh_info->sd_dma_mode) {
				case DMA_MODE_SDMA:
					sd_dma(("%s: SDMA: SysAddr reg was 0x%x now 0x%x\n",
					      __FUNCTION__, sdstd_rreg(sdioh_info, SD_SysAddr),
					     (uint32)sdioh_info->dma_phys));
				sdstd_wreg(sdioh_info, SD_SysAddr, sdioh_info->dma_phys);
					break;
				case DMA_MODE_ADMA1:
				case DMA_MODE_ADMA2:
					sd_dma(("%s: ADMA: Using ADMA\n", __FUNCTION__));
						sd_create_adma_descriptor(sdioh_info, 0,
						sdioh_info->dma_phys, blockcount*blocksize,
						ADMA2_ATTRIBUTE_VALID | ADMA2_ATTRIBUTE_END |
						ADMA2_ATTRIBUTE_INT | ADMA2_ATTRIBUTE_ACT_TRAN);
					/* Dump descriptor if DMA debugging is enabled. */
					if (sd_msglevel & SDH_DMA_VAL) {
						sd_dump_adma_dscr(sdioh_info);
					}

					sdstd_wreg(sdioh_info, SD_ADMA_SysAddr,
					           sdioh_info->adma2_dscr_phys);
					break;
				default:
					sd_err(("%s: unsupported DMA mode %d.\n",
						__FUNCTION__, sdioh_info->sd_dma_mode));
					break;
				}
			}

			sd_trace(("%s: Setting block count %d, block size %d bytes\n",
			          __FUNCTION__, blockcount, blocksize));
			sdstd_wreg16(sdioh_info, SD_BlockSize, blocksize);
			sdstd_wreg16(sdioh_info, SD_BlockCount, blockcount);

			xfer_reg = SFIELD(xfer_reg, XFER_DMA_ENABLE, use_dma);

			if (sdioh_info->client_block_size[func] != blocksize)
				set_client_block_size(sdioh_info, 1, blocksize);

			if (blockcount > 1) {
				xfer_reg = SFIELD(xfer_reg, XFER_MULTI_BLOCK, 1);
				xfer_reg = SFIELD(xfer_reg, XFER_BLK_COUNT_EN, 1);
				xfer_reg = SFIELD(xfer_reg, XFER_CMD_12_EN, 0);
			} else {
				xfer_reg = SFIELD(xfer_reg, XFER_MULTI_BLOCK, 0);
				xfer_reg = SFIELD(xfer_reg, XFER_BLK_COUNT_EN, 0);
				xfer_reg = SFIELD(xfer_reg, XFER_CMD_12_EN, 0);
			}

			if (GFIELD(cmd_arg, CMD53_RW_FLAG) == SDIOH_XFER_TYPE_READ)
				xfer_reg = SFIELD(xfer_reg, XFER_DATA_DIRECTION, 1);
			else
				xfer_reg = SFIELD(xfer_reg, XFER_DATA_DIRECTION, 0);

			retries = RETRIES_SMALL;
			while (GFIELD(sdstd_rreg(sdioh_info, SD_PresentState),
			              PRES_DAT_INHIBIT) && --retries)
				sd_err(("%s: Waiting for Data Inhibit cmd = %d\n",
				        __FUNCTION__, cmd));
			if (!retries) {
				sd_err(("%s: Data Inhibit timeout\n", __FUNCTION__));
				if (trap_errs)
					ASSERT(0);
				return ERROR;
			}
			sdstd_wreg16(sdioh_info, SD_TransferMode, xfer_reg);

		} else {	/* Non block mode */
			uint16 bytes = GFIELD(cmd_arg, CMD53_BYTE_BLK_CNT);
			/* The byte/block count field only has 9 bits,
			 * so, to do a 512-byte bytemode transfer, this
			 * field will contain 0, but we need to tell the
			 * controller we're transferring 512 bytes.
			 */
			if (bytes == 0) bytes = 512;

			if (use_dma)
				sdstd_wreg(sdioh_info, SD_SysAddr, sdioh_info->dma_phys);

			/* PCI: Transfer Mode register 0x0c */
			xfer_reg = SFIELD(xfer_reg, XFER_DMA_ENABLE, bytes <= 4 ? 0 : use_dma);
			xfer_reg = SFIELD(xfer_reg, XFER_CMD_12_EN, 0);
			if (GFIELD(cmd_arg, CMD53_RW_FLAG) == SDIOH_XFER_TYPE_READ)
				xfer_reg = SFIELD(xfer_reg, XFER_DATA_DIRECTION, 1);
			else
				xfer_reg = SFIELD(xfer_reg, XFER_DATA_DIRECTION, 0);
			/* See table 2-8 Host Controller spec ver 1.00 */
			xfer_reg = SFIELD(xfer_reg, XFER_BLK_COUNT_EN, 0); /* Dont care */
			xfer_reg = SFIELD(xfer_reg, XFER_MULTI_BLOCK, 0);

			sdstd_wreg16(sdioh_info, SD_BlockSize,  bytes);

			sdstd_wreg16(sdioh_info, SD_BlockCount, 1);

			retries = RETRIES_SMALL;
			while (GFIELD(sdstd_rreg(sdioh_info, SD_PresentState),
			              PRES_DAT_INHIBIT) && --retries)
				sd_err(("%s: Waiting for Data Inhibit cmd = %d\n",
				        __FUNCTION__, cmd));
			if (!retries) {
				sd_err(("%s: Data Inhibit timeout\n", __FUNCTION__));
				if (trap_errs)
					ASSERT(0);
				return ERROR;
			}
			sdstd_wreg16(sdioh_info, SD_TransferMode, xfer_reg);
		}
		break;

	default:
		sd_err(("%s: Unknown command\n", __FUNCTION__));
		return ERROR;
	}

	if (sdioh_info->sd_mode == SDIOH_MODE_SPI) {
		cmd_reg = SFIELD(cmd_reg, CMD_CRC_EN, 0);
		cmd_reg = SFIELD(cmd_reg, CMD_INDEX_EN, 0);
	}

	/* Setup and issue the SDIO command */
	sdstd_wreg(sdioh_info, SD_Arg0, arg);
	sdstd_wreg16(sdioh_info, SD_Command, cmd_reg);

	/* If we are in polled mode, wait for the command to complete.
	 * In interrupt mode, return immediately. The calling function will
	 * know that the command has completed when the CMDATDONE interrupt
	 * is asserted
	 */
	if (sdioh_info->polled_mode) {
		uint16 int_reg = 0;
		int retries = RETRIES_LARGE;

		do {
			int_reg = sdstd_rreg16(sdioh_info, SD_IntrStatus);
		} while (--retries &&
		         (GFIELD(int_reg, INTSTAT_ERROR_INT) == 0) &&
		         (GFIELD(int_reg, INTSTAT_CMD_COMPLETE) == 0));

		if (!retries) {
			sd_err(("%s: CMD_COMPLETE timeout: intrStatus: 0x%x "
			        "error stat 0x%x state 0x%x\n",
			        __FUNCTION__, int_reg,
			        sdstd_rreg16(sdioh_info, SD_ErrorIntrStatus),
			        sdstd_rreg(sdioh_info, SD_PresentState)));

			/* Attempt to reset CMD line when we get a CMD timeout */
			sdstd_wreg8(sdioh_info, SD_SoftwareReset, SFIELD(0, SW_RESET_CMD, 1));
			retries = RETRIES_LARGE;
			do {
				sd_trace(("%s: waiting for CMD line reset\n", __FUNCTION__));
			} while ((GFIELD(sdstd_rreg8(sdioh_info, SD_SoftwareReset),
			                 SW_RESET_CMD)) && retries--);

			if (!retries) {
				sd_err(("%s: Timeout waiting for CMD line reset\n", __FUNCTION__));
			}

			if (trap_errs)
				ASSERT(0);
			return (ERROR);
		}

		/* Clear Command Complete interrupt */
		int_reg = SFIELD(0, INTSTAT_CMD_COMPLETE, 1);
		sdstd_wreg16(sdioh_info, SD_IntrStatus, int_reg);

		/* Check for Errors */
		if (sdstd_check_errs(sdioh_info, cmd, arg)) {
			if (trap_errs)
				ASSERT(0);
			return ERROR;
		}
	}
	return SUCCESS;
}


static int
sdstd_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo, uint32 addr, int nbytes, uint32 *data)
{
	int status;
	uint32 cmd_arg;
	uint32 rsp5;
	uint16 int_reg, int_bit;
	uint flags;
	int num_blocks, blocksize;
	bool local_blockmode, local_dma;
	bool read = rw == SDIOH_READ ? 1 : 0;
	bool yield = FALSE;

	ASSERT(nbytes);

	cmd_arg = 0;

	sd_data(("%s: %s 53 addr 0x%x, len %d bytes, r_cnt %d t_cnt %d\n",
	         __FUNCTION__, read ? "Rd" : "Wr", addr, nbytes, sd->r_cnt, sd->t_cnt));

	if (read) sd->r_cnt++; else sd->t_cnt++;

	local_blockmode = sd->sd_blockmode;
	local_dma = USE_DMA(sd);

	/* Don't bother with block mode on small xfers */
	if (nbytes < sd->client_block_size[func]) {
		sd_data(("setting local blockmode to false: nbytes (%d) != block_size (%d)\n",
		         nbytes, sd->client_block_size[func]));
		local_blockmode = FALSE;
		local_dma = FALSE;
	}

	if (local_blockmode) {
		blocksize = MIN(sd->client_block_size[func], nbytes);
		num_blocks = nbytes/blocksize;
		cmd_arg = SFIELD(cmd_arg, CMD53_BYTE_BLK_CNT, num_blocks);
		cmd_arg = SFIELD(cmd_arg, CMD53_BLK_MODE, 1);
	} else {
		num_blocks =  1;
		blocksize = nbytes;
		cmd_arg = SFIELD(cmd_arg, CMD53_BYTE_BLK_CNT, nbytes);
		cmd_arg = SFIELD(cmd_arg, CMD53_BLK_MODE, 0);
	}

	if (local_dma && !read) {
		bcopy(data, sd->dma_buf, nbytes);
		sd_sync_dma(sd, read, nbytes);
	}

	if (fifo)
		cmd_arg = SFIELD(cmd_arg, CMD53_OP_CODE, 0);
	else
		cmd_arg = SFIELD(cmd_arg, CMD53_OP_CODE, 1);

	cmd_arg = SFIELD(cmd_arg, CMD53_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, CMD53_REG_ADDR, addr);
	if (read)
		cmd_arg = SFIELD(cmd_arg, CMD53_RW_FLAG, SDIOH_XFER_TYPE_READ);
	else
		cmd_arg = SFIELD(cmd_arg, CMD53_RW_FLAG, SDIOH_XFER_TYPE_WRITE);

	sd->data_xfer_count = nbytes;

	/* sdstd_cmd_issue() returns with the command complete bit
	 * in the ISR already cleared
	 */
	if ((status = sdstd_cmd_issue(sd, local_dma, SDIOH_CMD_53, cmd_arg)) != SUCCESS) {
		sd_err(("%s: cmd_issue failed for %s\n", __FUNCTION__, (read ? "read" : "write")));
		return status;
	}

	sdstd_cmd_getrsp(sd, &rsp5, 1);

	if ((flags = GFIELD(rsp5, RSP5_FLAGS)) != 0x10) {
		sd_err(("%s: Rsp5: nbytes %d, dma %d blockmode %d, read %d "
		        "numblocks %d, blocksize %d\n",
		        __FUNCTION__, nbytes, local_dma, local_dma, read, num_blocks, blocksize));

		if (flags & 1)
			sd_err(("%s: rsp5: Command not accepted: arg out of range 0x%x, "
			        "bytes %d dma %d\n",
			        __FUNCTION__, flags, GFIELD(cmd_arg, CMD53_BYTE_BLK_CNT),
			        GFIELD(cmd_arg, CMD53_BLK_MODE)));
		if (flags & 0x8)
			sd_err(("%s: Rsp5: General Error\n", __FUNCTION__));

		sd_err(("%s: rsp5 flags = 0x%x, expecting 0x10 returning error\n",
		        __FUNCTION__,  flags));
		if (trap_errs)
			ASSERT(0);
		return ERROR;
	}

	if (GFIELD(rsp5, RSP5_STUFF))
		sd_err(("%s: rsp5 stuff is 0x%x: expecting 0\n",
		        __FUNCTION__, GFIELD(rsp5, RSP5_STUFF)));

#ifdef BCMSDYIELD
	yield = sd_yieldcpu && ((uint)nbytes >= sd_minyield);
#endif

	if (!local_dma) {
		int bytes, i;
		uint32 tmp;
		for (i = 0; i < num_blocks; i++) {
			int words;

			/* Decide which status bit we're waiting for */
			if (read)
				int_bit = SFIELD(0, INTSTAT_BUF_READ_READY, 1);
			else
				int_bit = SFIELD(0, INTSTAT_BUF_WRITE_READY, 1);

			/* If not on, wait for it (or for xfer error) */
			int_reg = sdstd_rreg16(sd, SD_IntrStatus);
			if (!(int_reg & int_bit))
				int_reg = sdstd_waitbits(sd, int_bit, ERRINT_TRANSFER_ERRS, yield);

			/* Confirm we got the bit w/o error */
			if (!(int_reg & int_bit) || GFIELD(int_reg, INTSTAT_ERROR_INT)) {
				sd_err(("%s: Error or timeout for Buf_%s_Ready: intStat: 0x%x "
				        "errint: 0x%x PresentState 0x%x\n",
				        __FUNCTION__, read ? "Read" : "Write", int_reg,
				        sdstd_rreg16(sd, SD_ErrorIntrStatus),
				        sdstd_rreg(sd, SD_PresentState)));
				sdstd_dumpregs(sd);
				sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg);
				return (ERROR);
			}

			/* Clear Buf Ready bit */
			sdstd_wreg16(sd, SD_IntrStatus, int_bit);

			/* At this point we have Buffer Ready, write the data 4 bytes at a time */
			for (words = blocksize/4; words; words--) {
				if (read)
					*data = sdstd_rreg(sd, SD_BufferDataPort0);
				else
					sdstd_wreg(sd, SD_BufferDataPort0, *data);
				data++;
			}

			bytes = blocksize % 4;

			/* If no leftover bytes, go to next block */
			if (!bytes)
				continue;

			switch (bytes) {
			case 1:
				/* R/W 8 bits */
				if (read)
					*(data++) = (uint32)(sdstd_rreg8(sd, SD_BufferDataPort0));
				else
					sdstd_wreg8(sd, SD_BufferDataPort0,
					            (uint8)(*(data++) & 0xff));
				break;
			case 2:
				/* R/W 16 bits */
				if (read)
					*(data++) = (uint32)sdstd_rreg16(sd, SD_BufferDataPort0);
				else
					sdstd_wreg16(sd, SD_BufferDataPort0, (uint16)(*(data++)));
				break;
			case 3:
				/* R/W 24 bits:
				 * SD_BufferDataPort0[0-15] | SD_BufferDataPort1[16-23]
				 */
				if (read) {
					tmp = (uint32)sdstd_rreg16(sd, SD_BufferDataPort0);
					tmp |= ((uint32)(sdstd_rreg8(sd,
					                             SD_BufferDataPort1)) << 16);
					*(data++) = tmp;
				} else {
					tmp = *(data++);
					sdstd_wreg16(sd, SD_BufferDataPort0, (uint16)tmp & 0xffff);
					sdstd_wreg8(sd, SD_BufferDataPort1,
					            (uint8)((tmp >> 16) & 0xff));
				}
				break;
			default:
				sd_err(("%s: Unexpected bytes leftover %d\n",
				        __FUNCTION__, bytes));
				ASSERT(0);
				break;
			}
		}
	}	/* End PIO processing */

	/* Wait for Transfer Complete or Transfer Error */
	int_bit = SFIELD(0, INTSTAT_XFER_COMPLETE, 1);

	/* If not on, wait for it (or for xfer error) */
	int_reg = sdstd_rreg16(sd, SD_IntrStatus);
	if (!(int_reg & int_bit))
		int_reg = sdstd_waitbits(sd, int_bit, ERRINT_TRANSFER_ERRS, yield);

	/* Check for any errors from the data phase */
	if (sdstd_check_errs(sd, SDIOH_CMD_53, cmd_arg))
		return ERROR;

	/* May have gotten a software timeout if not blocking? */
	int_reg = sdstd_rreg16(sd, SD_IntrStatus);
	if (!(int_reg & int_bit)) {
		sd_err(("%s: Error or Timeout for xfer complete; %s, dma %d, State 0x%08x, "
		        "intr 0x%04x, Err 0x%04x, len = %d, rcnt %d, tcnt %d\n",
		        __FUNCTION__, read ? "R" : "W", local_dma,
		        sdstd_rreg(sd, SD_PresentState), int_reg,
		        sdstd_rreg16(sd, SD_ErrorIntrStatus), nbytes,
		        sd->r_cnt, sd->t_cnt));
		sdstd_dumpregs(sd);
		return ERROR;
	}

	/* Clear the status bits */
	int_reg = int_bit;
	if (local_dma) {
		/* DMA Complete */
		/* Reads in particular don't have DMA_COMPLETE set */
		int_reg = SFIELD(int_reg, INTSTAT_DMA_INT, 1);
	}
	sdstd_wreg16(sd, SD_IntrStatus, int_reg);

	/* Fetch data */
	if (local_dma && read) {
		sd_sync_dma(sd, read, nbytes);
		bcopy(sd->dma_buf, data, nbytes);
	}
	return SUCCESS;
}

static int
set_client_block_size(sdioh_info_t *sd, int func, int block_size)
{
	int base;
	int err = 0;


	sd_err(("%s: Setting block size %d, func %d\n", __FUNCTION__, block_size, func));
	sd->client_block_size[func] = block_size;

	/* Set the block size in the SDIO Card register */
	base = func * SDIOD_FBR_SIZE;
	err = sdstd_card_regwrite(sd, 0, base+SDIOD_CCCR_BLKSIZE_0, 1, block_size & 0xff);
	if (!err) {
		err = sdstd_card_regwrite(sd, 0, base+SDIOD_CCCR_BLKSIZE_1, 1,
		                          (block_size >> 8) & 0xff);
	}

	/* Do not set the block size in the SDIO Host register, that
	 * is func dependent and will get done on an individual
	 * transaction basis
	 */

	return (err ? BCME_SDIO_ERROR : 0);
}

/* Reset and re-initialize the device */
int
sdioh_sdio_reset(sdioh_info_t *si)
{
	uint8 hreg;

	/* Reset the attached device (use slower clock for safety) */
	sdstd_start_clock(si, 128);
	sdstd_reset(si, 0, 1);

	/* Reset portions of the host state accordingly */
	hreg = sdstd_rreg8(si, SD_HostCntrl);
	hreg = SFIELD(hreg, HOST_HI_SPEED_EN, 0);
	hreg = SFIELD(hreg, HOST_DATA_WIDTH, 0);
	si->sd_mode = SDIOH_MODE_SD1;

	/* Reinitialize the card */
	si->card_init_done = FALSE;
	return sdstd_client_init(si);
}


static void
sd_map_dma(sdioh_info_t * sd)
{

	void *va;

	if ((va = DMA_ALLOC_CONSISTENT(sd->osh, SD_PAGE,
		&sd->dma_start_phys, 0x12, 12)) == NULL) {
		sd->sd_dma_mode = DMA_MODE_NONE;
		sd->dma_start_buf = 0;
		sd->dma_buf = (void *)0;
		sd->dma_phys = 0;
		sd->alloced_dma_size = SD_PAGE;
		sd_err(("%s: DMA_ALLOC failed. Disabling DMA support.\n", __FUNCTION__));
	} else {
		sd->dma_start_buf = va;
		sd->dma_buf = (void *)ROUNDUP((uintptr)va, SD_PAGE);
		sd->dma_phys = ROUNDUP((sd->dma_start_phys), SD_PAGE);
		sd->alloced_dma_size = SD_PAGE;
		sd_err(("%s: Mapped DMA Buffer %dbytes @virt/phys: %p/0x%lx\n",
		        __FUNCTION__, sd->alloced_dma_size, sd->dma_buf, sd->dma_phys));
		sd_fill_dma_data_buf(sd, 0xA5);
	}

	if ((va = DMA_ALLOC_CONSISTENT(sd->osh, SD_PAGE,
		&sd->adma2_dscr_start_phys, 0x12, 12)) == NULL) {
		sd->sd_dma_mode = DMA_MODE_NONE;
		sd->adma2_dscr_start_buf = 0;
		sd->adma2_dscr_buf = (void *)0;
		sd->adma2_dscr_phys = 0;
		sd->alloced_adma2_dscr_size = 0;
		sd_err(("%s: DMA_ALLOC failed for descriptor buffer. "
		        "Disabling DMA support.\n", __FUNCTION__));
	} else {
		sd->adma2_dscr_start_buf = va;
		sd->adma2_dscr_buf = (void *)ROUNDUP((uintptr)va, SD_PAGE);
		sd->adma2_dscr_phys = ROUNDUP((sd->adma2_dscr_start_phys), SD_PAGE);
		sd->alloced_adma2_dscr_size = SD_PAGE;
	}

	sd_err(("%s: Mapped ADMA2 Descriptor Buffer %dbytes @virt/phys: %p/0x%lx\n",
	        __FUNCTION__, sd->alloced_adma2_dscr_size, sd->adma2_dscr_buf,
	        sd->adma2_dscr_phys));
	sd_clear_adma_dscr_buf(sd);
}

static void
sd_unmap_dma(sdioh_info_t * sd)
{
	if (sd->dma_start_buf) {
		DMA_FREE_CONSISTENT(sd->osh, sd->dma_start_buf, sd->alloced_dma_size,
			sd->dma_start_phys, 0x12);
	}

	if (sd->adma2_dscr_start_buf) {
		DMA_FREE_CONSISTENT(sd->osh, sd->adma2_dscr_start_buf, sd->alloced_adma2_dscr_size,
		                    sd->adma2_dscr_start_phys, 0x12);
	}
}

static void sd_clear_adma_dscr_buf(sdioh_info_t *sd)
{
	bzero((char *)sd->adma2_dscr_buf, SD_PAGE);
	sd_dump_adma_dscr(sd);
}

static void sd_fill_dma_data_buf(sdioh_info_t *sd, uint8 data)
{
	memset((char *)sd->dma_buf, data, SD_PAGE);
}


static void sd_create_adma_descriptor(sdioh_info_t *sd, uint32 index,
                                      uint32 addr_phys, uint16 length, uint16 flags)
{
	adma2_dscr_32b_t *adma2_dscr_table;
	adma1_dscr_t *adma1_dscr_table;

	adma2_dscr_table = sd->adma2_dscr_buf;
	adma1_dscr_table = sd->adma2_dscr_buf;

	switch (sd->sd_dma_mode) {
		case DMA_MODE_ADMA2:
			sd_dma(("%s: creating ADMA2 descriptor for index %d\n",
				__FUNCTION__, index));

			adma2_dscr_table[index].phys_addr = addr_phys;
			adma2_dscr_table[index].len_attr = length << 16;
			adma2_dscr_table[index].len_attr |= flags;
			break;
		case DMA_MODE_ADMA1:
			/* ADMA1 requires two descriptors, one for len
			 * and the other for data transfer
			 */
			index <<= 1;

			sd_dma(("%s: creating ADMA1 descriptor for index %d\n",
				__FUNCTION__, index));

			adma1_dscr_table[index].phys_addr_attr = length << 12;
			adma1_dscr_table[index].phys_addr_attr |= (ADMA1_ATTRIBUTE_ACT_SET |
			                                           ADMA2_ATTRIBUTE_VALID);
			adma1_dscr_table[index+1].phys_addr_attr = addr_phys & 0xFFFFF000;
			adma1_dscr_table[index+1].phys_addr_attr |= (flags & 0x3f);
			break;
		default:
			sd_err(("%s: cannot create ADMA descriptor for DMA mode %d\n",
				__FUNCTION__, sd->sd_dma_mode));
			break;
	}
}


static void sd_dump_adma_dscr(sdioh_info_t *sd)
{
	adma2_dscr_32b_t *adma2_dscr_table;
	adma1_dscr_t *adma1_dscr_table;
	uint32 i = 0;
	uint16 flags;
	char flags_str[32];

	ASSERT(sd->adma2_dscr_buf != NULL);

	adma2_dscr_table = sd->adma2_dscr_buf;
	adma1_dscr_table = sd->adma2_dscr_buf;

	switch (sd->sd_dma_mode) {
		case DMA_MODE_ADMA2:
			sd_err(("ADMA2 Descriptor Table (%dbytes) @virt/phys: %p/0x%lx\n",
				SD_PAGE, sd->adma2_dscr_buf, sd->adma2_dscr_phys));
			sd_err((" #[Descr VA  ]  Buffer PA  | Len    | Flags  (5:4  2   1   0)"
			        "     |\n"));
			while (adma2_dscr_table->len_attr & ADMA2_ATTRIBUTE_VALID) {
				flags = adma2_dscr_table->len_attr & 0xFFFF;
				sprintf(flags_str, "%s%s%s%s",
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_LINK) ? "LINK " :
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_TRAN) ? "TRAN " :
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_NOP) ? "NOP  " : "RSV  ",
					(flags & ADMA2_ATTRIBUTE_INT ? "INT " : "    "),
					(flags & ADMA2_ATTRIBUTE_END ? "END " : "    "),
					(flags & ADMA2_ATTRIBUTE_VALID ? "VALID" : ""));
				sd_err(("%2d[0x%p]: 0x%08x | 0x%04x | 0x%04x (%s) |\n",
				        i, adma2_dscr_table, adma2_dscr_table->phys_addr,
				        adma2_dscr_table->len_attr >> 16, flags, flags_str));
				i++;

				/* Follow LINK descriptors or skip to next. */
				if ((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
				     ADMA2_ATTRIBUTE_ACT_LINK) {
					adma2_dscr_table = phys_to_virt(
					    adma2_dscr_table->phys_addr);
				} else {
					adma2_dscr_table++;
				}

			}
			break;
		case DMA_MODE_ADMA1:
			sd_err(("ADMA1 Descriptor Table (%dbytes) @virt/phys: %p/0x%lx\n",
			         SD_PAGE, sd->adma2_dscr_buf, sd->adma2_dscr_phys));
			sd_err((" #[Descr VA  ]  Buffer PA  | Flags  (5:4  2   1   0)     |\n"));

			for (i = 0; adma1_dscr_table->phys_addr_attr & ADMA2_ATTRIBUTE_VALID; i++) {
				flags = adma1_dscr_table->phys_addr_attr & 0x3F;
				sprintf(flags_str, "%s%s%s%s",
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_LINK) ? "LINK " :
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_TRAN) ? "TRAN " :
					((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
					ADMA2_ATTRIBUTE_ACT_NOP) ? "NOP  " : "SET  ",
					(flags & ADMA2_ATTRIBUTE_INT ? "INT " : "    "),
					(flags & ADMA2_ATTRIBUTE_END ? "END " : "    "),
					(flags & ADMA2_ATTRIBUTE_VALID ? "VALID" : ""));
				sd_err(("%2d[0x%p]: 0x%08x | 0x%04x | (%s) |\n",
				        i, adma1_dscr_table,
				        adma1_dscr_table->phys_addr_attr & 0xFFFFF000,
				        flags, flags_str));

				/* Follow LINK descriptors or skip to next. */
				if ((flags & ADMA2_ATTRIBUTE_ACT_LINK) ==
				     ADMA2_ATTRIBUTE_ACT_LINK) {
					adma1_dscr_table = phys_to_virt(
						adma1_dscr_table->phys_addr_attr & 0xFFFFF000);
				} else {
					adma1_dscr_table++;
				}
			}
			break;
		default:
			sd_err(("Unknown DMA Descriptor Table Format.\n"));
			break;
	}
}

static void sdstd_dumpregs(sdioh_info_t *sd)
{
	sd_err(("IntrStatus:       0x%04x ErrorIntrStatus       0x%04x\n",
	            sdstd_rreg16(sd, SD_IntrStatus),
	            sdstd_rreg16(sd, SD_ErrorIntrStatus)));
	sd_err(("IntrStatusEnable: 0x%04x ErrorIntrStatusEnable 0x%04x\n",
	            sdstd_rreg16(sd, SD_IntrStatusEnable),
	            sdstd_rreg16(sd, SD_ErrorIntrStatusEnable)));
	sd_err(("IntrSignalEnable: 0x%04x ErrorIntrSignalEnable 0x%04x\n",
	            sdstd_rreg16(sd, SD_IntrSignalEnable),
	            sdstd_rreg16(sd, SD_ErrorIntrSignalEnable)));
}
