/*
 * Broadcom BCMSDH to SPI Protocol Conversion Layer
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
 * $Id: bcmsdspi.c,v 1.14.4.2.4.4.6.5 2010/03/10 03:09:48 Exp $
 */

#include <typedefs.h>

#include <bcmdevs.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <osl.h>
#include <siutils.h>
#include <sdio.h>		/* SDIO Device and Protocol Specs */
#include <sdioh.h>		/* SDIO Host Controller Specification */
#include <bcmsdbus.h>		/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>		/* ioctl/iovars */

#include <pcicfg.h>


#include <bcmsdspi.h>
#include <bcmspi.h>

#include <proto/sdspi.h>

#define SD_PAGE 4096

/* Globals */

uint sd_msglevel = SDH_ERROR_VAL;
uint sd_hiok = FALSE;		/* Use hi-speed mode if available? */
uint sd_sdmode = SDIOH_MODE_SPI;		/* Use SD4 mode by default */
uint sd_f2_blocksize = 512;	/* Default blocksize */

uint sd_divisor = 2;		/* Default 33MHz/2 = 16MHz for dongle */
uint sd_power = 1;		/* Default to SD Slot powered ON */
uint sd_clock = 1;		/* Default to SD Clock turned ON */
uint sd_crc = 0;		/* Default to SPI CRC Check turned OFF */
uint sd_pci_slot = 0xFFFFffff; /* Used to force selection of a particular PCI slot */

uint sd_toctl = 7;

/* Prototypes */
static bool sdspi_start_power(sdioh_info_t *sd);
static int sdspi_set_highspeed_mode(sdioh_info_t *sd, bool HSMode);
static int sdspi_card_enablefuncs(sdioh_info_t *sd);
static void sdspi_cmd_getrsp(sdioh_info_t *sd, uint32 *rsp_buffer, int count);
static int sdspi_cmd_issue(sdioh_info_t *sd, bool use_dma, uint32 cmd, uint32 arg,
                           uint32 *data, uint32 datalen);
static int sdspi_card_regread(sdioh_info_t *sd, int func, uint32 regaddr,
                              int regsize, uint32 *data);
static int sdspi_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr,
                               int regsize, uint32 data);
static int sdspi_driver_init(sdioh_info_t *sd);
static bool sdspi_reset(sdioh_info_t *sd, bool host_reset, bool client_reset);
static int sdspi_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo,
                          uint32 addr, int nbytes, uint32 *data);
static int sdspi_abort(sdioh_info_t *sd, uint func);

static int set_client_block_size(sdioh_info_t *sd, int func, int blocksize);

static uint8 sdspi_crc7(unsigned char* p, uint32 len);
static uint16 sdspi_crc16(unsigned char* p, uint32 len);
static int sdspi_crc_onoff(sdioh_info_t *sd, bool use_crc);

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
	sd->osh = osh;

	if (spi_osinit(sd) != 0) {
		sd_err(("%s: spi_osinit() failed\n", __FUNCTION__));
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return NULL;
	}

	sd->bar0 = (uintptr)bar0;
	sd->irq = irq;
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;
	sd->intr_handler_valid = FALSE;

	/* Set defaults */
	sd->sd_blockmode = FALSE;
	sd->use_client_ints = TRUE;
	sd->sd_use_dma = FALSE;	/* DMA Not supported */

	/* Haven't figured out how to make bytemode work with dma */
	if (!sd->sd_blockmode)
		sd->sd_use_dma = 0;

	if (!spi_hw_attach(sd)) {
		sd_err(("%s: spi_hw_attach() failed\n", __FUNCTION__));
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return NULL;
	}

	if (sdspi_driver_init(sd) != SUCCESS) {
		if (sdspi_driver_init(sd) != SUCCESS) {
			sd_err(("%s:sdspi_driver_init() failed()\n", __FUNCTION__));
			spi_hw_detach(sd);
			spi_osfree(sd);
			MFREE(sd->osh, sd, sizeof(sdioh_info_t));
			return (NULL);
		}
	}

	if (spi_register_irq(sd, irq) != SUCCESS) {
		sd_err(("%s: spi_register_irq() failed for irq = %d\n", __FUNCTION__, irq));
		spi_hw_detach(sd);
		spi_osfree(sd);
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
		if (sd->card_init_done)
			sdspi_reset(sd, 1, 1);

		sd_info(("%s: detaching from hardware\n", __FUNCTION__));
		spi_free_irq(sd->irq, sd);
		spi_hw_detach(sd);
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
	}

	return SDIOH_API_RC_SUCCESS;
}

/* Configure callback to client when we recieve client interrupt */
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
	return 0;
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
	IOV_CLOCK,
	IOV_CRC
};

const bcm_iovar_t sdioh_iovars[] = {
	{"sd_msglevel",	IOV_MSGLEVEL, 	0,	IOVT_UINT32,	0 },
	{"sd_blockmode", IOV_BLOCKMODE,	0,	IOVT_BOOL,	0 },
	{"sd_blocksize", IOV_BLOCKSIZE, 0,	IOVT_UINT32,	0 }, /* ((fn << 16) | size) */
	{"sd_dma",	IOV_DMA,	0,	IOVT_BOOL,	0 },
	{"sd_ints",	IOV_USEINTS,	0,	IOVT_BOOL,	0 },
	{"sd_numints",	IOV_NUMINTS,	0,	IOVT_UINT32,	0 },
	{"sd_numlocalints", IOV_NUMLOCALINTS, 0, IOVT_UINT32,	0 },
	{"sd_hostreg",	IOV_HOSTREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sd_devreg",	IOV_DEVREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t)	},
	{"sd_divisor",	IOV_DIVISOR,	0,	IOVT_UINT32,	0 },
	{"sd_power",	IOV_POWER,	0,	IOVT_UINT32,	0 },
	{"sd_clock",	IOV_CLOCK,	0,	IOVT_UINT32,	0 },
	{"sd_crc",	IOV_CRC,	0,	IOVT_UINT32,	0 },
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
			si->sd_use_dma = 0;
		break;

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
		spi_lock(si);
		bcmerror = set_client_block_size(si, func, blksize);
		spi_unlock(si);
		break;
	}

	case IOV_GVAL(IOV_DMA):
		int_val = (int32)si->sd_use_dma;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DMA):
		si->sd_use_dma = (bool)int_val;
		break;

	case IOV_GVAL(IOV_USEINTS):
		int_val = (int32)si->use_client_ints;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_USEINTS):
		break;

	case IOV_GVAL(IOV_DIVISOR):
		int_val = (uint32)sd_divisor;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DIVISOR):
		sd_divisor = int_val;
		if (!spi_start_clock(si, (uint16)sd_divisor)) {
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
		break;

	case IOV_GVAL(IOV_CLOCK):
		int_val = (uint32)sd_clock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CLOCK):
		sd_clock = int_val;
		break;

	case IOV_GVAL(IOV_CRC):
		int_val = (uint32)sd_crc;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CRC):
		/* Apply new setting, but don't change sd_crc until
		 * after the CRC-mode is selected in the device.  This
		 * is required because the software must generate a
		 * correct CRC for the CMD59 in order to be able to
		 * turn OFF the CRC.
		 */
		sdspi_crc_onoff(si, int_val ? 1 : 0);
		sd_crc = int_val;
		break;

	case IOV_GVAL(IOV_SDMODE):
		int_val = (uint32)sd_sdmode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDMODE):
		sd_sdmode = int_val;
		break;

	case IOV_GVAL(IOV_HISPEED):
		int_val = (uint32)sd_hiok;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HISPEED):
		sd_hiok = int_val;

		if (!sdspi_set_highspeed_mode(si, (bool)sd_hiok)) {
			sd_err(("Failed changing highspeed mode to %d.\n", sd_hiok));
			bcmerror = BCME_ERROR;
			return ERROR;
		}
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
		break;
	}

	case IOV_SVAL(IOV_HOSTREG):
	{
		sd_err(("IOV_HOSTREG unsupported\n"));
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

	spi_lock(sd);
	*cis = 0;
	for (count = 0; count < length; count++) {
		offset =  sd->func_cis_ptr[func] + count;
		if (sdspi_card_regread (sd, 0, offset, 1, &foo) < 0) {
			sd_err(("%s: regread failed: Can't read CIS\n", __FUNCTION__));
			spi_unlock(sd);
			return SDIOH_API_RC_FAIL;
		}
		*cis = (uint8)(foo & 0xff);
		cis++;
	}
	spi_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_request_byte(sdioh_info_t *sd, uint rw, uint func, uint regaddr, uint8 *byte)
{
	int status;
	uint32 cmd_arg;
	uint32 rsp5;

	spi_lock(sd);

	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, CMD52_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, CMD52_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, CMD52_RW_FLAG, rw == SDIOH_READ ? 0 : 1);
	cmd_arg = SFIELD(cmd_arg, CMD52_RAW, 0);
	cmd_arg = SFIELD(cmd_arg, CMD52_DATA, rw == SDIOH_READ ? 0 : *byte);

	sd_trace(("%s: rw=%d, func=%d, regaddr=0x%08x\n", __FUNCTION__, rw, func, regaddr));

	if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma,
	                              SDIOH_CMD_52, cmd_arg, NULL, 0)) != SUCCESS) {
		spi_unlock(sd);
		return status;
	}

	sdspi_cmd_getrsp(sd, &rsp5, 1);
	if (rsp5 != 0x00) {
		sd_err(("%s: rsp5 flags is 0x%x func=%d\n",
		        __FUNCTION__, rsp5, func));
		/* ASSERT(0); */
		spi_unlock(sd);
		return SDIOH_API_RC_FAIL;
	}

	if (rw == SDIOH_READ)
		*byte = sd->card_rsp_data >> 24;

	spi_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_request_word(sdioh_info_t *sd, uint cmd_type, uint rw, uint func, uint addr,
                   uint32 *word, uint nbytes)
{
	int status;

	spi_lock(sd);

	if (rw == SDIOH_READ)
		status = sdspi_card_regread(sd, func, addr, nbytes, word);
	else
		status = sdspi_card_regwrite(sd, func, addr, nbytes, *word);

	spi_unlock(sd);
	return (status == SUCCESS ?  SDIOH_API_RC_SUCCESS : SDIOH_API_RC_FAIL);
}

extern SDIOH_API_RC
sdioh_request_buffer(sdioh_info_t *sd, uint pio_dma, uint fix_inc, uint rw, uint func,
                     uint addr, uint reg_width, uint buflen_u, uint8 *buffer, void *pkt)
{
	int len;
	int buflen = (int)buflen_u;
	bool fifo = (fix_inc == SDIOH_DATA_FIX);

	spi_lock(sd);

	ASSERT(reg_width == 4);
	ASSERT(buflen_u < (1 << 30));
	ASSERT(sd->client_block_size[func]);

	sd_data(("%s: %c len %d r_cnt %d t_cnt %d, pkt @0x%p\n",
	         __FUNCTION__, rw == SDIOH_READ ? 'R' : 'W',
	         buflen_u, sd->r_cnt, sd->t_cnt, pkt));

	/* Break buffer down into blocksize chunks:
	 * Bytemode: 1 block at a time.
	 */
	while (buflen > 0) {
		if (sd->sd_blockmode) {
			/* Max xfer is Page size */
			len = MIN(SD_PAGE, buflen);

			/* Round down to a block boundry */
			if (buflen > sd->client_block_size[func])
				len = (len/sd->client_block_size[func]) *
				        sd->client_block_size[func];
		} else {
			/* Byte mode: One block at a time */
			len = MIN(sd->client_block_size[func], buflen);
		}

		if (sdspi_card_buf(sd, rw, func, fifo, addr, len, (uint32 *)buffer) != SUCCESS) {
			spi_unlock(sd);
			return SDIOH_API_RC_FAIL;
		}
		buffer += len;
		buflen -= len;
		if (!fifo)
			addr += len;
	}
	spi_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

static int
sdspi_abort(sdioh_info_t *sd, uint func)
{
	uint8 spi_databuf[] = { 0x74, 0x80, 0x00, 0x0C, 0xFF, 0x95, 0xFF, 0xFF,
	                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8 spi_rspbuf[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	int err = 0;

	sd_err(("Sending SPI Abort to F%d\n", func));
	spi_databuf[4] = func & 0x7;
	/* write to function 0, addr 6 (IOABORT) func # in 3 LSBs. */
	spi_sendrecv(sd, spi_databuf, spi_rspbuf, sizeof(spi_databuf));

	return err;
}

extern int
sdioh_abort(sdioh_info_t *sd, uint fnum)
{
	int ret;

	spi_lock(sd);
	ret = sdspi_abort(sd, fnum);
	spi_unlock(sd);

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


/*
 * Private/Static work routines
 */
static bool
sdspi_reset(sdioh_info_t *sd, bool host_reset, bool client_reset)
{
	if (!sd)
		return TRUE;

	spi_lock(sd);
	/* Reset client card */
	if (client_reset && (sd->adapter_slot != -1)) {
		if (sdspi_card_regwrite(sd, 0, SDIOD_CCCR_IOABORT, 1, 0x8) != SUCCESS)
			sd_err(("%s: Cannot write to card reg 0x%x\n",
			        __FUNCTION__, SDIOD_CCCR_IOABORT));
		else
			sd->card_rca = 0;
	}

	/* The host reset is a NOP in the sd-spi case. */
	if (host_reset) {
		sd->sd_mode = SDIOH_MODE_SPI;
	}
	spi_unlock(sd);
	return TRUE;
}

static int
sdspi_host_init(sdioh_info_t *sd)
{
	sdspi_reset(sd, 1, 0);

	/* Default power on mode is SD1 */
	sd->sd_mode = SDIOH_MODE_SPI;
	sd->polled_mode = TRUE;
	sd->host_init_done = TRUE;
	sd->card_init_done = FALSE;
	sd->adapter_slot = 1;

	return (SUCCESS);
}

#define CMD0_RETRIES 3
#define CMD5_RETRIES 10

static int
get_ocr(sdioh_info_t *sd, uint32 *cmd_arg, uint32 *cmd_rsp)
{
	uint32 rsp5;
	int retries, status;

	/* First issue a CMD0 to get the card into SPI mode. */
	for (retries = 0; retries <= CMD0_RETRIES; retries++) {
		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma,
		                              SDIOH_CMD_0, *cmd_arg, NULL, 0)) != SUCCESS) {
			sd_err(("%s: No response to CMD0\n", __FUNCTION__));
			continue;
		}

		sdspi_cmd_getrsp(sd, &rsp5, 1);

		if (GFIELD(rsp5, SPI_RSP_ILL_CMD)) {
			printf("%s: Card already initialized (continuing)\n", __FUNCTION__);
			break;
		}

		if (GFIELD(rsp5, SPI_RSP_IDLE)) {
			printf("%s: Card in SPI mode\n", __FUNCTION__);
			break;
		}
	}

	if (retries > CMD0_RETRIES) {
		sd_err(("%s: Too many retries for CMD0\n", __FUNCTION__));
		return ERROR;
	}

	/* Get the Card's Operation Condition. */
	/* Occasionally the board takes a while to become ready. */
	for (retries = 0; retries <= CMD5_RETRIES; retries++) {
		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma,
		                              SDIOH_CMD_5, *cmd_arg, NULL, 0)) != SUCCESS) {
			sd_err(("%s: No response to CMD5\n", __FUNCTION__));
			continue;
		}

		printf("CMD5 response data was: 0x%08x\n", sd->card_rsp_data);

		if (GFIELD(sd->card_rsp_data, RSP4_CARD_READY)) {
			printf("%s: Card ready\n", __FUNCTION__);
			break;
		}
	}

	if (retries > CMD5_RETRIES) {
		sd_err(("%s: Too many retries for CMD5\n", __FUNCTION__));
		return ERROR;
	}

	*cmd_rsp = sd->card_rsp_data;

	sdspi_crc_onoff(sd, sd_crc ? 1 : 0);

	return (SUCCESS);
}

static int
sdspi_crc_onoff(sdioh_info_t *sd, bool use_crc)
{
	uint32 args;
	int status;

	args = use_crc ? 1 : 0;
	if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma,
	                              SDIOH_CMD_59, args, NULL, 0)) != SUCCESS) {
		sd_err(("%s: No response to CMD59\n", __FUNCTION__));
	}

	sd_info(("CMD59 response data was: 0x%08x\n", sd->card_rsp_data));

	sd_err(("SD-SPI CRC turned %s\n", use_crc ? "ON" : "OFF"));
	return (SUCCESS);
}

static int
sdspi_client_init(sdioh_info_t *sd)
{
	uint8 fn_ints;

	sd_trace(("%s: Powering up slot %d\n", __FUNCTION__, sd->adapter_slot));

	/* Start at ~400KHz clock rate for initialization */
	if (!spi_start_clock(sd, 128)) {
		sd_err(("spi_start_clock failed\n"));
		return ERROR;
	}

	if (!sdspi_start_power(sd)) {
		sd_err(("sdspi_start_power failed\n"));
		return ERROR;
	}

	if (sd->num_funcs == 0) {
		sd_err(("%s: No IO funcs!\n", __FUNCTION__));
		return ERROR;
	}

	sdspi_card_enablefuncs(sd);

	set_client_block_size(sd, 1, BLOCK_SIZE_4318);
	fn_ints = INTR_CTL_FUNC1_EN;

	if (sd->num_funcs >= 2) {
		set_client_block_size(sd, 2, sd_f2_blocksize /* BLOCK_SIZE_4328 */);
		fn_ints |= INTR_CTL_FUNC2_EN;
	}

	/* Enable/Disable Client interrupts */
	/* Turn on here but disable at host controller */
	if (sdspi_card_regwrite(sd, 0, SDIOD_CCCR_INTEN, 1,
	                        (fn_ints | INTR_CTL_MASTER_EN)) != SUCCESS) {
		sd_err(("%s: Could not enable ints in CCCR\n", __FUNCTION__));
		return ERROR;
	}

	/* Switch to High-speed clocking mode if both host and device support it */
	sdspi_set_highspeed_mode(sd, (bool)sd_hiok);

	/* After configuring for High-Speed mode, set the desired clock rate. */
	if (!spi_start_clock(sd, (uint16)sd_divisor)) {
		sd_err(("spi_start_clock failed\n"));
		return ERROR;
	}

	sd->card_init_done = TRUE;

	return SUCCESS;
}

static int
sdspi_set_highspeed_mode(sdioh_info_t *sd, bool HSMode)
{
	uint32 regdata;
	int status;
	bool hsmode;

	if (HSMode == TRUE) {

		sd_err(("Attempting to enable High-Speed mode.\n"));

		if ((status = sdspi_card_regread(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
		                                 1, &regdata)) != SUCCESS) {
			return status;
		}
		if (regdata & SDIO_SPEED_SHS) {
			sd_err(("Device supports High-Speed mode.\n"));

			regdata |= SDIO_SPEED_EHS;

			sd_err(("Writing %08x to Card at %08x\n",
			         regdata, SDIOD_CCCR_SPEED_CONTROL));
			if ((status = sdspi_card_regwrite(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
			                                  1, regdata)) != BCME_OK) {
				return status;
			}

			hsmode = 1;

			sd_err(("High-speed clocking mode enabled.\n"));
		}
		else {
			sd_err(("Device does not support High-Speed Mode.\n"));
			hsmode = 0;
		}
	} else {
		if ((status = sdspi_card_regread(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
		                                 1, &regdata)) != SUCCESS) {
			return status;
		}

		regdata = ~SDIO_SPEED_EHS;

		sd_err(("Writing %08x to Card at %08x\n",
		         regdata, SDIOD_CCCR_SPEED_CONTROL));
		if ((status = sdspi_card_regwrite(sd, 0, SDIOD_CCCR_SPEED_CONTROL,
		                                  1, regdata)) != BCME_OK) {
			return status;
		}

		sd_err(("Low-speed clocking mode enabled.\n"));
		hsmode = 0;
	}

	spi_controller_highspeed_mode(sd, hsmode);

	return TRUE;
}

bool
sdspi_start_power(sdioh_info_t *sd)
{
	uint32 cmd_arg;
	uint32 cmd_rsp;

	sd_trace(("%s\n", __FUNCTION__));

	/* Get the Card's Operation Condition.  Occasionally the board
	 * takes a while to become ready
	 */

	cmd_arg = 0;
	if (get_ocr(sd, &cmd_arg, &cmd_rsp) != SUCCESS) {
		sd_err(("%s: Failed to get OCR; bailing\n", __FUNCTION__));
		return FALSE;
	}

	sd_err(("mem_present = %d\n", GFIELD(cmd_rsp, RSP4_MEM_PRESENT)));
	sd_err(("num_funcs = %d\n", GFIELD(cmd_rsp, RSP4_NUM_FUNCS)));
	sd_err(("card_ready = %d\n", GFIELD(cmd_rsp, RSP4_CARD_READY)));
	sd_err(("OCR = 0x%x\n", GFIELD(cmd_rsp, RSP4_IO_OCR)));

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


	return TRUE;
}

static int
sdspi_driver_init(sdioh_info_t *sd)
{
	sd_trace(("%s\n", __FUNCTION__));

	if ((sdspi_host_init(sd)) != SUCCESS) {
		return ERROR;
	}

	if (sdspi_client_init(sd) != SUCCESS) {
		return ERROR;
	}

	return SUCCESS;
}

static int
sdspi_card_enablefuncs(sdioh_info_t *sd)
{
	int status;
	uint32 regdata;
	uint32 regaddr, fbraddr;
	uint8 func;
	uint8 *ptr;

	sd_trace(("%s\n", __FUNCTION__));
	/* Get the Card's common CIS address */
	ptr = (uint8 *) &sd->com_cis_ptr;
	for (regaddr = SDIOD_CCCR_CISPTR_0; regaddr <= SDIOD_CCCR_CISPTR_2; regaddr++) {
		if ((status = sdspi_card_regread (sd, 0, regaddr, 1, &regdata)) != SUCCESS)
			return status;

		*ptr++ = (uint8) regdata;
	}

	/* Only the lower 17-bits are valid */
	sd->com_cis_ptr &= 0x0001FFFF;
	sd->func_cis_ptr[0] = sd->com_cis_ptr;
	sd_info(("%s: Card's Common CIS Ptr = 0x%x\n", __FUNCTION__, sd->com_cis_ptr));

	/* Get the Card's function CIS (for each function) */
	for (fbraddr = SDIOD_FBR_STARTADDR, func = 1;
	     func <= sd->num_funcs; func++, fbraddr += SDIOD_FBR_SIZE) {
		ptr = (uint8 *) &sd->func_cis_ptr[func];
		for (regaddr = SDIOD_FBR_CISPTR_0; regaddr <= SDIOD_FBR_CISPTR_2; regaddr++) {
			if ((status = sdspi_card_regread (sd, 0, regaddr + fbraddr, 1, &regdata))
			    != SUCCESS)
				return status;

			*ptr++ = (uint8) regdata;
		}

		/* Only the lower 17-bits are valid */
		sd->func_cis_ptr[func] &= 0x0001FFFF;
		sd_info(("%s: Function %d CIS Ptr = 0x%x\n",
		         __FUNCTION__, func, sd->func_cis_ptr[func]));
	}

	sd_info(("%s: write ESCI bit\n", __FUNCTION__));
	/* Enable continuous SPI interrupt (ESCI bit) */
	sdspi_card_regwrite(sd, 0, SDIOD_CCCR_BICTRL, 1, 0x60);

	sd_info(("%s: enable f1\n", __FUNCTION__));
	/* Enable function 1 on the card */
	regdata = SDIO_FUNC_ENABLE_1;
	if ((status = sdspi_card_regwrite(sd, 0, SDIOD_CCCR_IOEN, 1, regdata)) != SUCCESS)
		return status;

	sd_info(("%s: done\n", __FUNCTION__));
	return SUCCESS;
}

/* Read client card reg */
static int
sdspi_card_regread(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 *data)
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

		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma, SDIOH_CMD_52, cmd_arg, NULL, 0))
		    != SUCCESS)
			return status;

		sdspi_cmd_getrsp(sd, &rsp5, 1);

		if (rsp5 != 0x00)
			sd_err(("%s: rsp5 flags is 0x%x\t %d\n",
			        __FUNCTION__, rsp5, func));

		*data = sd->card_rsp_data >> 24;
	} else {
		cmd_arg = SFIELD(cmd_arg, CMD53_BYTE_BLK_CNT, regsize);
		cmd_arg = SFIELD(cmd_arg, CMD53_OP_CODE, 1);
		cmd_arg = SFIELD(cmd_arg, CMD53_BLK_MODE, 0);
		cmd_arg = SFIELD(cmd_arg, CMD53_FUNCTION, func);
		cmd_arg = SFIELD(cmd_arg, CMD53_REG_ADDR, regaddr);
		cmd_arg = SFIELD(cmd_arg, CMD53_RW_FLAG, SDIOH_XFER_TYPE_READ);

		sd->data_xfer_count = regsize;

		/* sdspi_cmd_issue() returns with the command complete bit
		 * in the ISR already cleared
		 */
		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma, SDIOH_CMD_53, cmd_arg, NULL, 0))
		    != SUCCESS)
			return status;

		sdspi_cmd_getrsp(sd, &rsp5, 1);

		if (rsp5 != 0x00)
			sd_err(("%s: rsp5 flags is 0x%x\t %d\n",
			        __FUNCTION__, rsp5, func));

		*data = sd->card_rsp_data;
		if (regsize == 2) {
			*data &= 0xffff;
		}

		sd_info(("%s: CMD53 func %d, addr 0x%x, size %d, data 0x%08x\n",
		         __FUNCTION__, func, regaddr, regsize, *data));


	}

	return SUCCESS;
}

/* write a client register */
static int
sdspi_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 data)
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
		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma, SDIOH_CMD_52, cmd_arg, NULL, 0))
		    != SUCCESS)
			return status;

		sdspi_cmd_getrsp(sd, &rsp5, 1);
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
		sd->cmd53_wr_data = data;

		sd_info(("%s: CMD53 func %d, addr 0x%x, size %d, data 0x%08x\n",
		         __FUNCTION__, func, regaddr, regsize, data));

		/* sdspi_cmd_issue() returns with the command complete bit
		 * in the ISR already cleared
		 */
		if ((status = sdspi_cmd_issue(sd, sd->sd_use_dma, SDIOH_CMD_53, cmd_arg, NULL, 0))
		    != SUCCESS)
			return status;

		sdspi_cmd_getrsp(sd, &rsp5, 1);

		if (rsp5 != 0x00)
			sd_err(("%s: rsp5 flags = 0x%x, expecting 0x00\n",
			        __FUNCTION__,  rsp5));

	}
	return SUCCESS;
}

void
sdspi_cmd_getrsp(sdioh_info_t *sd, uint32 *rsp_buffer, int count /* num 32 bit words */)
{
	*rsp_buffer = sd->card_response;
}

int max_errors = 0;

#define SPI_MAX_PKT_LEN		768
uint8	spi_databuf[SPI_MAX_PKT_LEN];
uint8	spi_rspbuf[SPI_MAX_PKT_LEN];

/* datalen is used for CMD53 length only (0 for sd->data_xfer_count) */
static int
sdspi_cmd_issue(sdioh_info_t *sd, bool use_dma, uint32 cmd, uint32 arg,
                uint32 *data, uint32 datalen)
{
	uint32 cmd_reg;
	uint32 cmd_arg = arg;
	uint8 cmd_crc = 0x95;		/* correct CRC for CMD0 and don't care for others. */
	uint16 dat_crc;
	uint8 cmd52data = 0;
	uint32 i, j;
	uint32 spi_datalen = 0;
	uint32 spi_pre_cmd_pad	= 0;
	uint32 spi_max_response_pad = 128;

	cmd_reg = 0;
	cmd_reg = SFIELD(cmd_reg, SPI_DIR, 1);
	cmd_reg = SFIELD(cmd_reg, SPI_CMD_INDEX, cmd);

	if (GFIELD(cmd_arg, CMD52_RW_FLAG) == 1) {	/* Same for CMD52 and CMD53 */
		cmd_reg = SFIELD(cmd_reg, SPI_RW, 1);
	}

	switch (cmd) {
	case SDIOH_CMD_59:	/* CRC_ON_OFF (SPI Mode Only) - Response R1 */
		cmd52data = arg & 0x1;
	case SDIOH_CMD_0:	/* Set Card to Idle State - No Response */
	case SDIOH_CMD_5:	/* Send Operation condition - Response R4 */
		sd_trace(("%s: CMD%d\n", __FUNCTION__, cmd));
		spi_datalen = 44;
		spi_pre_cmd_pad = 12;
		spi_max_response_pad = 28;
		break;

	case SDIOH_CMD_3:	/* Ask card to send RCA - Response R6 */
	case SDIOH_CMD_7:	/* Select card - Response R1 */
	case SDIOH_CMD_15:	/* Set card to inactive state - Response None */
		sd_err(("%s: CMD%d is invalid for SPI Mode.\n", __FUNCTION__, cmd));
		return ERROR;
		break;

	case SDIOH_CMD_52:	/* IO R/W Direct (single byte) - Response R5 */
		cmd52data = GFIELD(cmd_arg, CMD52_DATA);
		cmd_arg = arg;
		cmd_reg = SFIELD(cmd_reg, SPI_FUNC, GFIELD(cmd_arg, CMD52_FUNCTION));
		cmd_reg = SFIELD(cmd_reg, SPI_ADDR, GFIELD(cmd_arg, CMD52_REG_ADDR));
		/* Display trace for byte write */
		if (GFIELD(cmd_arg, CMD52_RW_FLAG) == 1) {
			sd_trace(("%s: CMD52: Wr F:%d @0x%04x=%02x\n",
			          __FUNCTION__,
			          GFIELD(cmd_arg, CMD52_FUNCTION),
			          GFIELD(cmd_arg, CMD52_REG_ADDR),
			          cmd52data));
		}

		spi_datalen = 32;
		spi_max_response_pad = 28;

		break;
	case SDIOH_CMD_53:	/* IO R/W Extended (multiple bytes/blocks) */
		cmd_arg = arg;
		cmd_reg = SFIELD(cmd_reg, SPI_FUNC, GFIELD(cmd_arg, CMD53_FUNCTION));
		cmd_reg = SFIELD(cmd_reg, SPI_ADDR, GFIELD(cmd_arg, CMD53_REG_ADDR));
		cmd_reg = SFIELD(cmd_reg, SPI_BLKMODE, 0);
		cmd_reg = SFIELD(cmd_reg, SPI_OPCODE, GFIELD(cmd_arg, CMD53_OP_CODE));
		cmd_reg = SFIELD(cmd_reg, SPI_STUFF0, (sd->data_xfer_count>>8));
		cmd52data = (uint8)sd->data_xfer_count;

		/* Set upper bit in byte count if necessary, but don't set it for 512 bytes. */
		if ((sd->data_xfer_count > 255) && (sd->data_xfer_count < 512)) {
			cmd_reg |= 1;
		}

		if (GFIELD(cmd_reg, SPI_RW) == 1) { /* Write */
			spi_max_response_pad = 32;
			spi_datalen = (sd->data_xfer_count + spi_max_response_pad) & 0xFFFC;
		} else { /* Read */

			spi_max_response_pad = 32;
			spi_datalen = (sd->data_xfer_count + spi_max_response_pad) & 0xFFFC;
		}
		sd_trace(("%s: CMD53: %s F:%d @0x%04x len=0x%02x\n",
		          __FUNCTION__,
		          (GFIELD(cmd_reg, SPI_RW) == 1 ? "Wr" : "Rd"),
		          GFIELD(cmd_arg, CMD53_FUNCTION),
		          GFIELD(cmd_arg, CMD53_REG_ADDR),
		          cmd52data));
		break;

	default:
		sd_err(("%s: Unknown command %d\n", __FUNCTION__, cmd));
		return ERROR;
	}

	/* Set up and issue the SDIO command */
	memset(spi_databuf, SDSPI_IDLE_PAD, spi_datalen);
	spi_databuf[spi_pre_cmd_pad + 0] = (cmd_reg & 0xFF000000) >> 24;
	spi_databuf[spi_pre_cmd_pad + 1] = (cmd_reg & 0x00FF0000) >> 16;
	spi_databuf[spi_pre_cmd_pad + 2] = (cmd_reg & 0x0000FF00) >> 8;
	spi_databuf[spi_pre_cmd_pad + 3] = (cmd_reg & 0x000000FF);
	spi_databuf[spi_pre_cmd_pad + 4] = cmd52data;

	/* Generate CRC7 for command, if CRC is enabled, otherwise, a
	 * default CRC7 of 0x95, which is correct for CMD0, is used.
	 */
	if (sd_crc) {
		cmd_crc = sdspi_crc7(&spi_databuf[spi_pre_cmd_pad], 5);
	}
	spi_databuf[spi_pre_cmd_pad + 5] = cmd_crc;
#define SPI_STOP_TRAN		0xFD

	/* for CMD53 Write, put the data into the output buffer  */
	if ((cmd == SDIOH_CMD_53) && (GFIELD(cmd_arg, CMD53_RW_FLAG) == 1)) {
		if (datalen != 0) {
			spi_databuf[spi_pre_cmd_pad + 9] = SDSPI_IDLE_PAD;
			spi_databuf[spi_pre_cmd_pad + 10] = SDSPI_START_BLOCK;

			for (i = 0; i < sd->data_xfer_count; i++) {
				spi_databuf[i + 11 + spi_pre_cmd_pad] = ((uint8 *)data)[i];
			}
			if (sd_crc) {
				dat_crc = sdspi_crc16(&spi_databuf[spi_pre_cmd_pad+11], i);
			} else {
				dat_crc = 0xAAAA;
			}
			spi_databuf[i + 11 + spi_pre_cmd_pad] = (dat_crc >> 8) & 0xFF;
			spi_databuf[i + 12 + spi_pre_cmd_pad] = dat_crc & 0xFF;
		} else if (sd->data_xfer_count == 2) {
			spi_databuf[spi_pre_cmd_pad + 9] = SDSPI_IDLE_PAD;
			spi_databuf[spi_pre_cmd_pad + 10] = SDSPI_START_BLOCK;
			spi_databuf[spi_pre_cmd_pad + 11]  = sd->cmd53_wr_data & 0xFF;
			spi_databuf[spi_pre_cmd_pad + 12] = (sd->cmd53_wr_data & 0x0000FF00) >> 8;
			if (sd_crc) {
				dat_crc = sdspi_crc16(&spi_databuf[spi_pre_cmd_pad+11], 2);
			} else {
				dat_crc = 0x22AA;
			}
			spi_databuf[spi_pre_cmd_pad + 13] = (dat_crc >> 8) & 0xFF;
			spi_databuf[spi_pre_cmd_pad + 14] = (dat_crc & 0xFF);
		} else if (sd->data_xfer_count == 4) {
			spi_databuf[spi_pre_cmd_pad + 9] = SDSPI_IDLE_PAD;
			spi_databuf[spi_pre_cmd_pad + 10] = SDSPI_START_BLOCK;
			spi_databuf[spi_pre_cmd_pad + 11]  = sd->cmd53_wr_data & 0xFF;
			spi_databuf[spi_pre_cmd_pad + 12] = (sd->cmd53_wr_data & 0x0000FF00) >> 8;
			spi_databuf[spi_pre_cmd_pad + 13] = (sd->cmd53_wr_data & 0x00FF0000) >> 16;
			spi_databuf[spi_pre_cmd_pad + 14] = (sd->cmd53_wr_data & 0xFF000000) >> 24;
			if (sd_crc) {
				dat_crc = sdspi_crc16(&spi_databuf[spi_pre_cmd_pad+11], 4);
			} else {
				dat_crc = 0x44AA;
			}
			spi_databuf[spi_pre_cmd_pad + 15] = (dat_crc >> 8) & 0xFF;
			spi_databuf[spi_pre_cmd_pad + 16] = (dat_crc & 0xFF);
		} else {
			printf("CMD53 Write: size %d unsupported\n", sd->data_xfer_count);
		}
	}

	spi_sendrecv(sd, spi_databuf, spi_rspbuf, spi_datalen);

	for (i = spi_pre_cmd_pad + SDSPI_COMMAND_LEN; i < spi_max_response_pad; i++) {
		if ((spi_rspbuf[i] & SDSPI_START_BIT_MASK) == 0) {
			break;
		}
	}

	if (i == spi_max_response_pad) {
		sd_err(("%s: Did not get a response for CMD%d\n", __FUNCTION__, cmd));
		return ERROR;
	}

	/* Extract the response. */
	sd->card_response = spi_rspbuf[i];

	/* for CMD53 Read, find the start of the response data... */
	if ((cmd == SDIOH_CMD_53) && (GFIELD(cmd_arg, CMD52_RW_FLAG) == 0)) {
		for (; i < spi_max_response_pad; i++) {
			if (spi_rspbuf[i] == SDSPI_START_BLOCK) {
				break;
			}
		}

		if (i == spi_max_response_pad) {
			printf("Did not get a start of data phase for CMD%d\n", cmd);
			max_errors++;
			sdspi_abort(sd, GFIELD(cmd_arg, CMD53_FUNCTION));
		}
		sd->card_rsp_data = spi_rspbuf[i+1];
		sd->card_rsp_data |= spi_rspbuf[i+2] << 8;
		sd->card_rsp_data |= spi_rspbuf[i+3] << 16;
		sd->card_rsp_data |= spi_rspbuf[i+4] << 24;

		if (datalen != 0) {
			i++;
			for (j = 0; j < sd->data_xfer_count; j++) {
				((uint8 *)data)[j] = spi_rspbuf[i+j];
			}
			if (sd_crc) {
				uint16 recv_crc;

				recv_crc = spi_rspbuf[i+j] << 8 | spi_rspbuf[i+j+1];
				dat_crc = sdspi_crc16((uint8 *)data, datalen);
				if (dat_crc != recv_crc) {
					sd_err(("%s: Incorrect data CRC: expected 0x%04x, "
					        "received 0x%04x\n",
					        __FUNCTION__, dat_crc, recv_crc));
				}
			}
		}
		return SUCCESS;
	}

	sd->card_rsp_data = spi_rspbuf[i+4];
	sd->card_rsp_data |= spi_rspbuf[i+3] << 8;
	sd->card_rsp_data |= spi_rspbuf[i+2] << 16;
	sd->card_rsp_data |= spi_rspbuf[i+1] << 24;

	/* Display trace for byte read */
	if ((cmd == SDIOH_CMD_52) && (GFIELD(cmd_arg, CMD52_RW_FLAG) == 0)) {
		sd_trace(("%s: CMD52: Rd F:%d @0x%04x=%02x\n",
		          __FUNCTION__,
		          GFIELD(cmd_arg, CMD53_FUNCTION),
		          GFIELD(cmd_arg, CMD53_REG_ADDR),
		          sd->card_rsp_data >> 24));
	}

	return SUCCESS;
}

/*
 * On entry: if single-block or non-block, buffer size <= block size.
 * If multi-block, buffer size is unlimited.
 * Question is how to handle the left-overs in either single- or multi-block.
 * I think the caller should break the buffer up so this routine will always
 * use block size == buffer size to handle the end piece of the buffer
 */

static int
sdspi_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo, uint32 addr, int nbytes, uint32 *data)
{
	int status;
	uint32 cmd_arg;
	uint32 rsp5;
	int num_blocks, blocksize;
	bool local_blockmode, local_dma;
	bool read = rw == SDIOH_READ ? 1 : 0;

	ASSERT(nbytes);

	cmd_arg = 0;
	sd_data(("%s: %s 53 func %d, %s, addr 0x%x, len %d bytes, r_cnt %d t_cnt %d\n",
	         __FUNCTION__, read ? "Rd" : "Wr", func, fifo ? "FIXED" : "INCR",
	         addr, nbytes, sd->r_cnt, sd->t_cnt));

	if (read) sd->r_cnt++; else sd->t_cnt++;

	local_blockmode = sd->sd_blockmode;
	local_dma = sd->sd_use_dma;

	/* Don't bother with block mode on small xfers */
	if (nbytes < sd->client_block_size[func]) {
		sd_info(("setting local blockmode to false: nbytes (%d) != block_size (%d)\n",
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
	if ((func == 2) && (fifo == 1)) {
		sd_data(("%s: %s 53 func %d, %s, addr 0x%x, len %d bytes, r_cnt %d t_cnt %d\n",
		         __FUNCTION__, read ? "Rd" : "Wr", func, fifo ? "FIXED" : "INCR",
		         addr, nbytes, sd->r_cnt, sd->t_cnt));
	}

	/* sdspi_cmd_issue() returns with the command complete bit
	 * in the ISR already cleared
	 */
	if ((status = sdspi_cmd_issue(sd, local_dma,
	                              SDIOH_CMD_53, cmd_arg,
	                              data, nbytes)) != SUCCESS) {
		sd_err(("%s: cmd_issue failed for %s\n", __FUNCTION__, (read ? "read" : "write")));
		return status;
	}

	sdspi_cmd_getrsp(sd, &rsp5, 1);

	if (rsp5 != 0x00) {
		sd_err(("%s: rsp5 flags = 0x%x, expecting 0x00\n",
		        __FUNCTION__,  rsp5));
		return ERROR;
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
	err = sdspi_card_regwrite(sd, 0, base + SDIOD_CCCR_BLKSIZE_0, 1, block_size & 0xff);
	if (!err) {
		err = sdspi_card_regwrite(sd, 0, base + SDIOD_CCCR_BLKSIZE_1, 1,
		                          (block_size >> 8) & 0xff);
	}

	/*
	 * Do not set the block size in the SDIO Host register; that
	 * is func dependent and will get done on an individual
	 * transaction basis.
	 */

	return (err ? BCME_SDIO_ERROR : 0);
}

/* Reset and re-initialize the device */
int
sdioh_sdio_reset(sdioh_info_t *si)
{
	si->card_init_done = FALSE;
	return sdspi_client_init(si);
}

#define CRC7_POLYNOM	0x09
#define CRC7_CRCHIGHBIT	0x40

static uint8 sdspi_crc7(unsigned char* p, uint32 len)
{
	uint8 c, j, bit, crc = 0;
	uint32 i;

	for (i = 0; i < len; i++) {
		c = *p++;
		for (j = 0x80; j; j >>= 1) {
			bit = crc & CRC7_CRCHIGHBIT;
			crc <<= 1;
			if (c & j) bit ^= CRC7_CRCHIGHBIT;
			if (bit) crc ^= CRC7_POLYNOM;
		}
	}

	/* Convert the CRC7 to an 8-bit SD CRC */
	crc = (crc << 1) | 1;

	return (crc);
}

#define CRC16_POLYNOM	0x1021
#define CRC16_CRCHIGHBIT	0x8000

static uint16 sdspi_crc16(unsigned char* p, uint32 len)
{
	uint32 i;
	uint16 j, c, bit;
	uint16 crc = 0;

	for (i = 0; i < len; i++) {
		c = *p++;
		for (j = 0x80; j; j >>= 1) {
			bit = crc & CRC16_CRCHIGHBIT;
			crc <<= 1;
			if (c & j) bit ^= CRC16_CRCHIGHBIT;
			if (bit) crc ^= CRC16_POLYNOM;
		}
	}

	return (crc);
}
