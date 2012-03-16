/*
 *  BCMSDH interface glue
 *  implement bcmsdh API for SDIOH driver
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 * $Id: bcmsdh.c 300445 2011-12-03 05:37:20Z $
 */

/**
 * @file bcmsdh.c
 */

/* ****************** BCMSDH Interface Functions *************************** */

#include <typedefs.h>
#include <bcmdevs.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <siutils.h>
#include <osl.h>

#include <bcmsdh.h>	/* BRCM API for SDIO clients (such as wl, dhd) */
#include <bcmsdbus.h>	/* common SDIO/controller interface */
#include <sbsdio.h>	/* SDIO device core hardware definitions. */

#include <sdio.h>	/* SDIO Device and Protocol Specs */

#define SDIOH_API_ACCESS_RETRY_LIMIT	2
const uint bcmsdh_msglevel = BCMSDH_ERROR_VAL;

/**
 * BCMSDH API context
 */
struct bcmsdh_info
{
	bool	init_success;	/* underlying driver successfully attached */
	void	*sdioh;		/* handler for sdioh */
	uint32  vendevid;	/* Target Vendor and Device ID on SD bus */
	osl_t   *osh;
	bool	regfail;	/* Save status of last reg_read/reg_write call */
	uint32	sbwad;		/* Save backplane window address */
};
/* local copy of bcm sd handler */
bcmsdh_info_t * l_bcmsdh = NULL;

#if defined(OOB_INTR_ONLY) && defined(HW_OOB)
extern int
sdioh_enable_hw_oob_intr(void *sdioh, bool enable);

void
bcmsdh_enable_hw_oob_intr(bcmsdh_info_t *sdh, bool enable)
{
	sdioh_enable_hw_oob_intr(sdh->sdioh, enable);
}
#endif

/* Attach BCMSDH layer to SDIO Host Controller Driver
 *
 * @param osh OSL Handle.
 * @param cfghdl Configuration Handle.
 * @param regsva Virtual address of controller registers.
 * @param irq Interrupt number of SDIO controller.
 *
 * @return bcmsdh_info_t Handle to BCMSDH context.
 */
bcmsdh_info_t *
bcmsdh_attach(osl_t *osh, void *cfghdl, void **regsva, uint irq)
{
	bcmsdh_info_t *bcmsdh;

	if ((bcmsdh = (bcmsdh_info_t *)MALLOC(osh, sizeof(bcmsdh_info_t))) == NULL) {
		BCMSDH_ERROR(("bcmsdh_attach: out of memory, malloced %d bytes\n", MALLOCED(osh)));
		return NULL;
	}
	bzero((char *)bcmsdh, sizeof(bcmsdh_info_t));

	/* save the handler locally */
	l_bcmsdh = bcmsdh;

	if (!(bcmsdh->sdioh = sdioh_attach(osh, cfghdl, irq))) {
		bcmsdh_detach(osh, bcmsdh);
		return NULL;
	}

	bcmsdh->osh = osh;
	bcmsdh->init_success = TRUE;

	*regsva = (uint32 *)SI_ENUM_BASE;

	/* Report the BAR, to fix if needed */
	bcmsdh->sbwad = SI_ENUM_BASE;
	return bcmsdh;
}

int
bcmsdh_detach(osl_t *osh, void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	if (bcmsdh != NULL) {
		if (bcmsdh->sdioh) {
			sdioh_detach(osh, bcmsdh->sdioh);
			bcmsdh->sdioh = NULL;
		}
		MFREE(osh, bcmsdh, sizeof(bcmsdh_info_t));
	}

	l_bcmsdh = NULL;
	return 0;
}

int
bcmsdh_iovar_op(void *sdh, const char *name,
                void *params, int plen, void *arg, int len, bool set)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	return sdioh_iovar_op(bcmsdh->sdioh, name, params, plen, arg, len, set);
}

bool
bcmsdh_intr_query(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	bool on;

	ASSERT(bcmsdh);
	status = sdioh_interrupt_query(bcmsdh->sdioh, &on);
	if (SDIOH_API_SUCCESS(status))
		return FALSE;
	else
		return on;
}

int
bcmsdh_intr_enable(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	ASSERT(bcmsdh);

	status = sdioh_interrupt_set(bcmsdh->sdioh, TRUE);
	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

int
bcmsdh_intr_disable(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	ASSERT(bcmsdh);

	status = sdioh_interrupt_set(bcmsdh->sdioh, FALSE);
	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

int
bcmsdh_intr_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	ASSERT(bcmsdh);

	status = sdioh_interrupt_register(bcmsdh->sdioh, fn, argh);
	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

int
bcmsdh_intr_dereg(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	ASSERT(bcmsdh);

	status = sdioh_interrupt_deregister(bcmsdh->sdioh);
	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

#if defined(DHD_DEBUG)
bool
bcmsdh_intr_pending(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	ASSERT(sdh);
	return sdioh_interrupt_pending(bcmsdh->sdioh);
}
#endif


int
bcmsdh_devremove_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh)
{
	ASSERT(sdh);

	/* don't support yet */
	return BCME_UNSUPPORTED;
}

/**
 * Read from SDIO Configuration Space
 * @param sdh SDIO Host context.
 * @param func_num Function number to read from.
 * @param addr Address to read from.
 * @param err Error return.
 * @return value read from SDIO configuration space.
 */
uint8
bcmsdh_cfg_read(void *sdh, uint fnc_num, uint32 addr, int *err)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	int32 retry = 0;
#endif
	uint8 data = 0;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			OSL_DELAY(1000);
#endif
	status = sdioh_cfg_read(bcmsdh->sdioh, fnc_num, addr, (uint8 *)&data);
#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	} while (!SDIOH_API_SUCCESS(status) && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
#endif
	if (err)
		*err = (SDIOH_API_SUCCESS(status) ? 0 : BCME_SDIO_ERROR);

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, uint8data = 0x%x\n", __FUNCTION__,
	            fnc_num, addr, data));

	return data;
}

void
bcmsdh_cfg_write(void *sdh, uint fnc_num, uint32 addr, uint8 data, int *err)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	int32 retry = 0;
#endif

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	do {
		if (retry)	/* wait for 1 ms till bus get settled down */
			OSL_DELAY(1000);
#endif
	status = sdioh_cfg_write(bcmsdh->sdioh, fnc_num, addr, (uint8 *)&data);
#ifdef SDIOH_API_ACCESS_RETRY_LIMIT
	} while (!SDIOH_API_SUCCESS(status) && (retry++ < SDIOH_API_ACCESS_RETRY_LIMIT));
#endif
	if (err)
		*err = SDIOH_API_SUCCESS(status) ? 0 : BCME_SDIO_ERROR;

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, uint8data = 0x%x\n", __FUNCTION__,
	            fnc_num, addr, data));
}

uint32
bcmsdh_cfg_read_word(void *sdh, uint fnc_num, uint32 addr, int *err)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	uint32 data = 0;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

	status = sdioh_request_word(bcmsdh->sdioh, SDIOH_CMD_TYPE_NORMAL, SDIOH_READ, fnc_num,
	                            addr, &data, 4);

	if (err)
		*err = (SDIOH_API_SUCCESS(status) ? 0 : BCME_SDIO_ERROR);

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, uint32data = 0x%x\n", __FUNCTION__,
	            fnc_num, addr, data));

	return data;
}

void
bcmsdh_cfg_write_word(void *sdh, uint fnc_num, uint32 addr, uint32 data, int *err)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

	status = sdioh_request_word(bcmsdh->sdioh, SDIOH_CMD_TYPE_NORMAL, SDIOH_WRITE, fnc_num,
	                            addr, &data, 4);

	if (err)
		*err = (SDIOH_API_SUCCESS(status) ? 0 : BCME_SDIO_ERROR);

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, uint32data = 0x%x\n", __FUNCTION__, fnc_num,
	             addr, data));
}


int
bcmsdh_cis_read(void *sdh, uint func, uint8 *cis, uint length)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;

	uint8 *tmp_buf, *tmp_ptr;
	uint8 *ptr;
	bool ascii = func & ~0xf;
	func &= 0x7;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);
	ASSERT(cis);
	ASSERT(length <= SBSDIO_CIS_SIZE_LIMIT);

	status = sdioh_cis_read(bcmsdh->sdioh, func, cis, length);

	if (ascii) {
		/* Move binary bits to tmp and format them into the provided buffer. */
		if ((tmp_buf = (uint8 *)MALLOC(bcmsdh->osh, length)) == NULL) {
			BCMSDH_ERROR(("%s: out of memory\n", __FUNCTION__));
			return BCME_NOMEM;
		}
		bcopy(cis, tmp_buf, length);
		for (tmp_ptr = tmp_buf, ptr = cis; ptr < (cis + length - 4); tmp_ptr++) {
			ptr += sprintf((char*)ptr, "%.2x ", *tmp_ptr & 0xff);
			if ((((tmp_ptr - tmp_buf) + 1) & 0xf) == 0)
				ptr += sprintf((char *)ptr, "\n");
		}
		MFREE(bcmsdh->osh, tmp_buf, length);
	}

	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}


int
bcmsdhsdio_set_sbaddr_window(void *sdh, uint32 address, bool force_set)
{
	int err = 0;
	uint bar0 = address & ~SBSDIO_SB_OFT_ADDR_MASK;
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	if (bar0 != bcmsdh->sbwad || force_set) {
		bcmsdh_cfg_write(bcmsdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
			(address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
		if (!err)
			bcmsdh_cfg_write(bcmsdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID,
				(address >> 16) & SBSDIO_SBADDRMID_MASK, &err);
		if (!err)
			bcmsdh_cfg_write(bcmsdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH,
				(address >> 24) & SBSDIO_SBADDRHIGH_MASK, &err);

		if (!err)
			bcmsdh->sbwad = bar0;
		else
			/* invalidate cached window var */
			bcmsdh->sbwad = 0;

	}

	return err;
}

uint32
bcmsdh_reg_read(void *sdh, uint32 addr, uint size)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	uint32 word = 0;

	BCMSDH_INFO(("%s:fun = 1, addr = 0x%x, ", __FUNCTION__, addr));

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

	if (bcmsdhsdio_set_sbaddr_window(bcmsdh, addr, FALSE))
		return 0xFFFFFFFF;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = sdioh_request_word(bcmsdh->sdioh, SDIOH_CMD_TYPE_NORMAL,
		SDIOH_READ, SDIO_FUNC_1, addr, &word, size);

	bcmsdh->regfail = !(SDIOH_API_SUCCESS(status));

	BCMSDH_INFO(("uint32data = 0x%x\n", word));

	/* if ok, return appropriately masked word */
	if (SDIOH_API_SUCCESS(status)) {
		switch (size) {
			case sizeof(uint8):
				return (word & 0xff);
			case sizeof(uint16):
				return (word & 0xffff);
			case sizeof(uint32):
				return word;
			default:
				bcmsdh->regfail = TRUE;

		}
	}

	/* otherwise, bad sdio access or invalid size */
	BCMSDH_ERROR(("%s: error reading addr 0x%04x size %d\n", __FUNCTION__, addr, size));
	return 0xFFFFFFFF;
}

uint32
bcmsdh_reg_write(void *sdh, uint32 addr, uint size, uint32 data)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	int err = 0;

	BCMSDH_INFO(("%s:fun = 1, addr = 0x%x, uint%ddata = 0x%x\n",
	             __FUNCTION__, addr, size*8, data));

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	ASSERT(bcmsdh->init_success);

	if ((err = bcmsdhsdio_set_sbaddr_window(bcmsdh, addr, FALSE)))
		return err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	if (size == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;
	status = sdioh_request_word(bcmsdh->sdioh, SDIOH_CMD_TYPE_NORMAL, SDIOH_WRITE, SDIO_FUNC_1,
	                            addr, &data, size);
	bcmsdh->regfail = !(SDIOH_API_SUCCESS(status));

	if (SDIOH_API_SUCCESS(status))
		return 0;

	BCMSDH_ERROR(("%s: error writing 0x%08x to addr 0x%04x size %d\n",
	              __FUNCTION__, data, addr, size));
	return 0xFFFFFFFF;
}

bool
bcmsdh_regfail(void *sdh)
{
	return ((bcmsdh_info_t *)sdh)->regfail;
}

int
bcmsdh_recv_buf(void *sdh, uint32 addr, uint fn, uint flags,
                uint8 *buf, uint nbytes, void *pkt,
                bcmsdh_cmplt_fn_t complete_fn, void *handle)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	uint incr_fix;
	uint width;
	int err = 0;

	ASSERT(bcmsdh);
	ASSERT(bcmsdh->init_success);

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
	             __FUNCTION__, fn, addr, nbytes));

	/* Async not implemented yet */
	ASSERT(!(flags & SDIO_REQ_ASYNC));
	if (flags & SDIO_REQ_ASYNC)
		return BCME_UNSUPPORTED;

	if ((err = bcmsdhsdio_set_sbaddr_window(bcmsdh, addr, FALSE)))
		return err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = sdioh_request_buffer(bcmsdh->sdioh, SDIOH_DATA_PIO, incr_fix,
	                              SDIOH_READ, fn, addr, width, nbytes, buf, pkt);

	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_SDIO_ERROR);
}

int
bcmsdh_send_buf(void *sdh, uint32 addr, uint fn, uint flags,
                uint8 *buf, uint nbytes, void *pkt,
                bcmsdh_cmplt_fn_t complete_fn, void *handle)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;
	uint incr_fix;
	uint width;
	int err = 0;

	ASSERT(bcmsdh);
	ASSERT(bcmsdh->init_success);

	BCMSDH_INFO(("%s:fun = %d, addr = 0x%x, size = %d\n",
	            __FUNCTION__, fn, addr, nbytes));

	/* Async not implemented yet */
	ASSERT(!(flags & SDIO_REQ_ASYNC));
	if (flags & SDIO_REQ_ASYNC)
		return BCME_UNSUPPORTED;

	if ((err = bcmsdhsdio_set_sbaddr_window(bcmsdh, addr, FALSE)))
		return err;

	addr &= SBSDIO_SB_OFT_ADDR_MASK;

	incr_fix = (flags & SDIO_REQ_FIXED) ? SDIOH_DATA_FIX : SDIOH_DATA_INC;
	width = (flags & SDIO_REQ_4BYTE) ? 4 : 2;
	if (width == 4)
		addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = sdioh_request_buffer(bcmsdh->sdioh, SDIOH_DATA_PIO, incr_fix,
	                              SDIOH_WRITE, fn, addr, width, nbytes, buf, pkt);

	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

int
bcmsdh_rwdata(void *sdh, uint rw, uint32 addr, uint8 *buf, uint nbytes)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	SDIOH_API_RC status;

	ASSERT(bcmsdh);
	ASSERT(bcmsdh->init_success);
	ASSERT((addr & SBSDIO_SBWINDOW_MASK) == 0);

	addr &= SBSDIO_SB_OFT_ADDR_MASK;
	addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	status = sdioh_request_buffer(bcmsdh->sdioh, SDIOH_DATA_PIO, SDIOH_DATA_INC,
	                              (rw ? SDIOH_WRITE : SDIOH_READ), SDIO_FUNC_1,
	                              addr, 4, nbytes, buf, NULL);

	return (SDIOH_API_SUCCESS(status) ? 0 : BCME_ERROR);
}

int
bcmsdh_abort(void *sdh, uint fn)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	return sdioh_abort(bcmsdh->sdioh, fn);
}

int
bcmsdh_start(void *sdh, int stage)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	return sdioh_start(bcmsdh->sdioh, stage);
}

int
bcmsdh_stop(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	return sdioh_stop(bcmsdh->sdioh);
}

int
bcmsdh_waitlockfree(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	return sdioh_waitlockfree(bcmsdh->sdioh);
}


int
bcmsdh_query_device(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;
	bcmsdh->vendevid = (VENDOR_BROADCOM << 16) | 0;
	return (bcmsdh->vendevid);
}

uint
bcmsdh_query_iofnum(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	return (sdioh_query_iofnum(bcmsdh->sdioh));
}

int
bcmsdh_reset(bcmsdh_info_t *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	return sdioh_sdio_reset(bcmsdh->sdioh);
}

void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh)
{
	ASSERT(sdh);
	return sdh->sdioh;
}

/* Function to pass device-status bits to DHD. */
uint32
bcmsdh_get_dstatus(void *sdh)
{
	return 0;
}
uint32
bcmsdh_cur_sbwad(void *sdh)
{
	bcmsdh_info_t *bcmsdh = (bcmsdh_info_t *)sdh;

	if (!bcmsdh)
		bcmsdh = l_bcmsdh;

	return (bcmsdh->sbwad);
}

void
bcmsdh_chipinfo(void *sdh, uint32 chip, uint32 chiprev)
{
	return;
}


int
bcmsdh_sleep(void *sdh, bool enab)
{
#ifdef SDIOH_SLEEP_ENABLED
	bcmsdh_info_t *p = (bcmsdh_info_t *)sdh;
	sdioh_info_t *sd = (sdioh_info_t *)(p->sdioh);

	return sdioh_sleep(sd, enab);
#else
	return BCME_UNSUPPORTED;
#endif
}

int
bcmsdh_gpio_init(void *sdh)
{
	bcmsdh_info_t *p = (bcmsdh_info_t *)sdh;
	sdioh_info_t *sd = (sdioh_info_t *)(p->sdioh);

	return sdioh_gpio_init(sd);
}

bool
bcmsdh_gpioin(void *sdh, uint32 gpio)
{
	bcmsdh_info_t *p = (bcmsdh_info_t *)sdh;
	sdioh_info_t *sd = (sdioh_info_t *)(p->sdioh);

	return sdioh_gpioin(sd, gpio);
}

int
bcmsdh_gpioouten(void *sdh, uint32 gpio)
{
	bcmsdh_info_t *p = (bcmsdh_info_t *)sdh;
	sdioh_info_t *sd = (sdioh_info_t *)(p->sdioh);

	return sdioh_gpioouten(sd, gpio);
}

int
bcmsdh_gpioout(void *sdh, uint32 gpio, bool enab)
{
	bcmsdh_info_t *p = (bcmsdh_info_t *)sdh;
	sdioh_info_t *sd = (sdioh_info_t *)(p->sdioh);

	return sdioh_gpioout(sd, gpio, enab);
}
