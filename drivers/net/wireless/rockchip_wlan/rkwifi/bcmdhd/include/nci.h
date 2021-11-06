/*
 * Misc utility routines for accessing chip-specific features
 * of the BOOKER NCI (non coherent interconnect) based Broadcom chips.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 */

#ifndef _NCI_H
#define _NCI_H

#include <siutils.h>

#ifdef SOCI_NCI_BUS
void nci_uninit(void *nci);
uint32 nci_scan(si_t *sih);
void nci_dump_erom(void *nci);
void* nci_init(si_t *sih, chipcregs_t *cc, uint bustype);
volatile void *nci_setcore(si_t *sih, uint coreid, uint coreunit);
volatile void *nci_setcoreidx(si_t *sih, uint coreidx);
uint nci_findcoreidx(const si_t *sih, uint coreid, uint coreunit);
volatile uint32 *nci_corereg_addr(si_t *sih, uint coreidx, uint regoff);
uint nci_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
uint nci_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val);
uint nci_corerev_minor(const si_t *sih);
uint nci_corerev(const si_t *sih);
uint nci_corevendor(const si_t *sih);
uint nci_get_wrap_reg(const si_t *sih, uint32 offset, uint32 mask, uint32 val);
void nci_core_reset(const si_t *sih, uint32 bits, uint32 resetbits);
void nci_core_disable(const si_t *sih, uint32 bits);
bool nci_iscoreup(const si_t *sih);
uint32 nci_coreid(const si_t *sih, uint coreidx);
uint nci_numcoreunits(const si_t *sih, uint coreid);
uint32 nci_addr_space(const si_t *sih, uint spidx, uint baidx);
uint32 nci_addr_space_size(const si_t *sih, uint spidx, uint baidx);
bool nci_iscoreup(const si_t *sih);
uint nci_intflag(si_t *sih);
uint nci_flag(si_t *sih);
uint nci_flag_alt(const si_t *sih);
void nci_setint(const si_t *sih, int siflag);
uint32 nci_oobr_baseaddr(const si_t *sih, bool second);
uint nci_coreunit(const si_t *sih);
uint nci_corelist(const si_t *sih, uint coreid[]);
int nci_numaddrspaces(const si_t *sih);
uint32 nci_addrspace(const si_t *sih, uint spidx, uint baidx);
uint32 nci_addrspacesize(const si_t *sih, uint spidx, uint baidx);
void nci_coreaddrspaceX(const si_t *sih, uint asidx, uint32 *addr, uint32 *size);
uint32 nci_core_cflags(const si_t *sih, uint32 mask, uint32 val);
void nci_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val);
uint32 nci_core_sflags(const si_t *sih, uint32 mask, uint32 val);
uint nci_wrapperreg(const si_t *sih, uint32 offset, uint32 mask, uint32 val);
void nci_invalidate_second_bar0win(si_t *sih);
int nci_backplane_access(si_t *sih, uint addr, uint size, uint *val, bool read);
int nci_backplane_access_64(si_t *sih, uint addr, uint size, uint64 *val, bool read);
uint nci_num_slaveports(const si_t *sih, uint coreid);
#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP)
void nci_dumpregs(const si_t *sih, struct bcmstrbuf *b);
#endif  /* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */
#ifdef BCMDBG
void nci_view(si_t *sih, bool verbose);
void nci_viewall(si_t *sih, bool verbose);
#endif /* BCMDBG */
uint32 nci_get_nth_wrapper(const si_t *sih, int32 wrap_pos);
uint32 nci_get_axi_addr(const si_t *sih, uint32 *size);
uint32* nci_wrapper_dump_binary_one(const si_info_t *sii, uint32 *p32, uint32 wrap_ba);
uint32 nci_wrapper_dump_binary(const si_t *sih, uchar *p);
uint32 nci_wrapper_dump_last_timeout(const si_t *sih, uint32 *error,
	uint32 *core, uint32 *ba, uchar *p);
bool nci_check_enable_backplane_log(const si_t *sih);
uint32 nci_get_core_baaddr(const si_t *sih, uint32 *size, int32 baidx);
uint32 nci_clear_backplane_to(si_t *sih);
uint32 nci_clear_backplane_to_per_core(si_t *sih, uint coreid, uint coreunit, void *wrap);
bool nci_ignore_errlog(const si_info_t *sii, const aidmp_t *ai,
	uint32 lo_addr, uint32 hi_addr, uint32 err_axi_id, uint32 errsts);
void nci_wrapper_get_last_error(const si_t *sih, uint32 *error_status, uint32 *core, uint32 *lo,
	uint32 *hi, uint32 *id);
uint32 nci_get_axi_timeout_reg(void);
uint32 nci_findcoreidx_by_axiid(const si_t *sih, uint32 axiid);
uint32* nci_wrapper_dump_binary_one(const si_info_t *sii, uint32 *p32, uint32 wrap_ba);
uint32 nci_wrapper_dump_binary(const si_t *sih, uchar *p);
uint32 nci_wrapper_dump_last_timeout(const si_t *sih, uint32 *error,
	uint32 *core, uint32 *ba, uchar *p);
bool nci_check_enable_backplane_log(const si_t *sih);
uint32 ai_wrapper_dump_buf_size(const si_t *sih);
uint32 nci_wrapper_dump_buf_size(const si_t *sih);
#endif /* SOCI_NCI_BUS */
#endif /* _NCI_H */
