/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Utility routines for configuring different memories in Broadcom chips.
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: $
 */

#include <typedefs.h>
#include <sbchipc.h>
#include <hndsoc.h>
#include <bcmdevs.h>
#include <osl.h>
#include <sbgci.h>
#include <siutils.h>
#include <bcmutils.h>
#include <hndmem.h>

#define IS_MEMTYPE_VALID(mem)	((mem >= MEM_SOCRAM) && (mem < MEM_MAX))
#define IS_MEMCONFIG_VALID(cfg)	((cfg >= PDA_CONFIG_CLEAR) && (cfg < PDA_CONFIG_MAX))

/* Returns the number of banks in a given memory */
int
hndmem_num_banks(si_t *sih, int mem)
{
	uint32 savecore, mem_info;
	int num_banks = 0;
	gciregs_t *gciregs;
	osl_t *osh = si_osh(sih);

	if (!IS_MEMTYPE_VALID(mem)) {
		goto exit;
	}

	savecore = si_coreidx(sih);

	/* TODO: Check whether SOCRAM core is present or not. If not, bail out */
	/* In future we need to add code for TCM based chips as well */
	if (!si_setcore(sih, SOCRAM_CORE_ID, 0)) {
		goto exit;
	}

	if (sih->gcirev >= 9) {
		gciregs = si_setcore(sih, GCI_CORE_ID, 0);

		mem_info = R_REG(osh, &gciregs->wlan_mem_info);

		switch (mem) {
			case MEM_SOCRAM:
				num_banks = (mem_info & WLAN_MEM_INFO_REG_NUMSOCRAMBANKS_MASK) >>
						WLAN_MEM_INFO_REG_NUMSOCRAMBANKS_SHIFT;
				break;
			case MEM_BM:
				num_banks = (mem_info & WLAN_MEM_INFO_REG_NUMD11MACBM_MASK) >>
						WLAN_MEM_INFO_REG_NUMD11MACBM_SHIFT;
				break;
			case MEM_UCM:
				num_banks = (mem_info & WLAN_MEM_INFO_REG_NUMD11MACUCM_MASK) >>
						WLAN_MEM_INFO_REG_NUMD11MACUCM_SHIFT;
				break;
			case MEM_SHM:
				num_banks = (mem_info & WLAN_MEM_INFO_REG_NUMD11MACSHM_MASK) >>
						WLAN_MEM_INFO_REG_NUMD11MACSHM_SHIFT;
				break;
			default:
				ASSERT(0);
				break;
		}
	} else {
		/* TODO: Figure out bank information using SOCRAM registers */
	}

	si_setcoreidx(sih, savecore);
exit:
	return num_banks;
}

/* Returns the size of a give bank in a given memory */
int
hndmem_bank_size(si_t *sih, hndmem_type_t mem, int bank_num)
{
	uint32 savecore, bank_info, reg_data;
	int bank_sz = 0;
	gciregs_t *gciregs;
	osl_t *osh = si_osh(sih);

	if (!IS_MEMTYPE_VALID(mem)) {
		goto exit;
	}

	savecore = si_coreidx(sih);

	/* TODO: Check whether SOCRAM core is present or not. If not, bail out */
	/* In future we need to add code for TCM based chips as well */
	if (!si_setcore(sih, SOCRAM_CORE_ID, 0)) {
		goto exit;
	}

	if (sih->gcirev >= 9) {
		gciregs = si_setcore(sih, GCI_CORE_ID, 0);

		reg_data = ((mem &
				GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_MASK) <<
				GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_SHIFT) |
				((bank_num & GCI_INDIRECT_ADDRESS_REG_REGINDEX_MASK)
				 << GCI_INDIRECT_ADDRESS_REG_REGINDEX_SHIFT);
		W_REG(osh, &gciregs->gci_indirect_addr, reg_data);

		bank_info = R_REG(osh, &gciregs->wlan_bankxinfo);
		bank_sz = (bank_info & WLAN_BANKXINFO_BANK_SIZE_MASK) >>
			WLAN_BANKXINFO_BANK_SIZE_SHIFT;
	} else {
		/* TODO: Figure out bank size using SOCRAM registers */
	}

	si_setcoreidx(sih, savecore);
exit:
	return bank_sz;
}

/* Returns the start address of given memory */
uint32
hndmem_mem_base(si_t *sih, hndmem_type_t mem)
{
	uint32 savecore, base_addr = 0;

	/* Currently only support of SOCRAM is available in hardware */
	if (mem != MEM_SOCRAM) {
		goto exit;
	}

	savecore = si_coreidx(sih);

	if (si_setcore(sih, SOCRAM_CORE_ID, 0))
	{
		base_addr = si_get_slaveport_addr(sih, CORE_SLAVE_PORT_1,
			CORE_BASE_ADDR_0, SOCRAM_CORE_ID, 0);
	} else {
		/* TODO: Add code to get the base address of TCM */
		base_addr = 0;
	}

	si_setcoreidx(sih, savecore);

exit:
	return base_addr;
}

#ifdef BCMDEBUG
char *hndmem_type_str[] =
	{
		"SOCRAM",	/* 0 */
		"BM",		/* 1 */
		"UCM",		/* 2 */
		"SHM",		/* 3 */
	};

/* Dumps the complete memory information */
void
hndmem_dump_meminfo_all(si_t *sih)
{
	int mem, bank, bank_cnt, bank_sz;

	for (mem = MEM_SOCRAM; mem < MEM_MAX; mem++) {
		bank_cnt = hndmem_num_banks(sih, mem);

		printf("\nMemtype: %s\n", hndmem_type_str[mem]);
		for (bank = 0; bank < bank_cnt; bank++) {
			bank_sz = hndmem_bank_size(sih, mem, bank);
			printf("Bank-%d: %d KB\n", bank, bank_sz);
		}
	}
}
#endif /* BCMDEBUG */

/* Configures the Sleep PDA for a particular bank for a given memory type */
int
hndmem_sleeppda_bank_config(si_t *sih, hndmem_type_t mem, int bank_num,
		hndmem_config_t config, uint32 pda)
{
	uint32 savecore, reg_data;
	gciregs_t *gciregs;
	int err = BCME_OK;
	osl_t *osh = si_osh(sih);

	/* TODO: Check whether SOCRAM core is present or not. If not, bail out */
	/* In future we need to add code for TCM based chips as well */
	if (!si_setcore(sih, SOCRAM_CORE_ID, 0)) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	/* Sleep PDA is supported only by GCI rev >= 9 */
	if (sih->gcirev < 9) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (!IS_MEMTYPE_VALID(mem)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	if (!IS_MEMCONFIG_VALID(config)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	savecore = si_coreidx(sih);
	gciregs = si_setcore(sih, GCI_CORE_ID, 0);

	reg_data = ((mem &
			GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_MASK) <<
			GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_SHIFT) |
			((bank_num & GCI_INDIRECT_ADDRESS_REG_REGINDEX_MASK)
			 << GCI_INDIRECT_ADDRESS_REG_REGINDEX_SHIFT);

	W_REG(osh, &gciregs->gci_indirect_addr, reg_data);

	if (config == PDA_CONFIG_SET_PARTIAL) {
		W_REG(osh, &gciregs->wlan_bankxsleeppda, pda);
		W_REG(osh, &gciregs->wlan_bankxkill, 0);
	}
	else if (config == PDA_CONFIG_SET_FULL) {
		W_REG(osh, &gciregs->wlan_bankxsleeppda, WLAN_BANKX_SLEEPPDA_REG_SLEEPPDA_MASK);
		W_REG(osh, &gciregs->wlan_bankxkill, WLAN_BANKX_PKILL_REG_SLEEPPDA_MASK);
	} else {
		W_REG(osh, &gciregs->wlan_bankxsleeppda, 0);
		W_REG(osh, &gciregs->wlan_bankxkill, 0);
	}

	si_setcoreidx(sih, savecore);

exit:
	return err;
}

/* Configures the Active PDA for a particular bank for a given memory type */
int
hndmem_activepda_bank_config(si_t *sih, hndmem_type_t mem,
		int bank_num, hndmem_config_t config, uint32 pda)
{
	uint32 savecore, reg_data;
	gciregs_t *gciregs;
	int err = BCME_OK;
	osl_t *osh = si_osh(sih);

	if (!IS_MEMTYPE_VALID(mem)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	if (!IS_MEMCONFIG_VALID(config)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	savecore = si_coreidx(sih);

	/* TODO: Check whether SOCRAM core is present or not. If not, bail out */
	/* In future we need to add code for TCM based chips as well */
	if (!si_setcore(sih, SOCRAM_CORE_ID, 0)) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (sih->gcirev >= 9) {
		gciregs = si_setcore(sih, GCI_CORE_ID, 0);

		reg_data = ((mem &
				GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_MASK) <<
				GCI_INDIRECT_ADDRESS_REG_GPIOINDEX_SHIFT) |
				((bank_num & GCI_INDIRECT_ADDRESS_REG_REGINDEX_MASK)
				 << GCI_INDIRECT_ADDRESS_REG_REGINDEX_SHIFT);

		W_REG(osh, &gciregs->gci_indirect_addr, reg_data);

		if (config == PDA_CONFIG_SET_PARTIAL) {
			W_REG(osh, &gciregs->wlan_bankxactivepda, pda);
		}
		else if (config == PDA_CONFIG_SET_FULL) {
			W_REG(osh, &gciregs->wlan_bankxactivepda,
					WLAN_BANKX_SLEEPPDA_REG_SLEEPPDA_MASK);
		} else {
			W_REG(osh, &gciregs->wlan_bankxactivepda, 0);
		}
	} else {
		/* TODO: Configure SOCRAM PDA using SOCRAM registers */
		err = BCME_UNSUPPORTED;
	}

	si_setcoreidx(sih, savecore);

exit:
	return err;
}

/* Configures the Sleep PDA for all the banks for a given memory type */
int
hndmem_sleeppda_config(si_t *sih, hndmem_type_t mem, hndmem_config_t config)
{
	int bank;
	int num_banks = hndmem_num_banks(sih, mem);
	int err = BCME_OK;

	/* Sleep PDA is supported only by GCI rev >= 9 */
	if (sih->gcirev < 9) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (!IS_MEMTYPE_VALID(mem)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	if (!IS_MEMCONFIG_VALID(config)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	for (bank = 0; bank < num_banks; bank++)
	{
		err = hndmem_sleeppda_bank_config(sih, mem, bank, config, 0);
	}

exit:
	return err;
}

/* Configures the Active PDA for all the banks for a given memory type */
int
hndmem_activepda_config(si_t *sih, hndmem_type_t mem, hndmem_config_t config)
{
	int bank;
	int num_banks = hndmem_num_banks(sih, mem);
	int err = BCME_OK;

	if (!IS_MEMTYPE_VALID(mem)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	if (!IS_MEMCONFIG_VALID(config)) {
		err = BCME_BADOPTION;
		goto exit;
	}

	for (bank = 0; bank < num_banks; bank++)
	{
		err = hndmem_activepda_bank_config(sih, mem, bank, config, 0);
	}

exit:
	return err;
}

/* Turn off/on all the possible banks in a given memory range.
 * Currently this works only for SOCRAM as this is restricted by HW.
 */
int
hndmem_activepda_mem_config(si_t *sih, hndmem_type_t mem, uint32 mem_start,
		uint32 size, hndmem_config_t config)
{
	int bank, bank_sz, num_banks;
	int mem_end;
	int bank_start_addr, bank_end_addr;
	int err = BCME_OK;

	/* We can get bank size for only SOCRAM/TCM only. Support is not avilable
	 * for other memories (BM, UCM and SHM)
	 */
	if (mem != MEM_SOCRAM) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	num_banks = hndmem_num_banks(sih, mem);
	bank_start_addr = hndmem_mem_base(sih, mem);
	mem_end = mem_start + size - 1;

	for (bank = 0; bank < num_banks; bank++)
	{
		/* Bank size is spcified in bankXinfo register in terms on KBs */
		bank_sz = 1024 * hndmem_bank_size(sih, mem, bank);

		bank_end_addr = bank_start_addr + bank_sz - 1;

		if (config == PDA_CONFIG_SET_FULL) {
			/* Check if the bank is completely overlapping with the given mem range */
			if ((mem_start <= bank_start_addr) && (mem_end >= bank_end_addr)) {
				err = hndmem_activepda_bank_config(sih, mem, bank, config, 0);
			}
		} else {
			/* Check if the bank is completely overlaped with the given mem range */
			if (((mem_start <= bank_start_addr) && (mem_end >= bank_end_addr)) ||
				/* Check if the bank is partially overlaped with the given range */
				((mem_start <= bank_end_addr) && (mem_end >= bank_start_addr))) {
				err = hndmem_activepda_bank_config(sih, mem, bank, config, 0);
			}
		}

		bank_start_addr += bank_sz;
	}

exit:
	return err;
}
