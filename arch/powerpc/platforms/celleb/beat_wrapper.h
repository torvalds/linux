/*
 * Beat hypervisor call I/F
 *
 * (C) Copyright 2007 TOSHIBA CORPORATION
 *
 * This code is based on arch/powerpc/platforms/pseries/plpar_wrapper.h.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef BEAT_HCALL
#include "beat_syscall.h"

/* defined in hvCall.S */
extern s64 beat_hcall_norets(u64 opcode, ...);
extern s64 beat_hcall_norets8(u64 opcode, u64 arg1, u64 arg2, u64 arg3,
	u64 arg4, u64 arg5, u64 arg6, u64 arg7, u64 arg8);
extern s64 beat_hcall1(u64 opcode, u64 retbuf[1], ...);
extern s64 beat_hcall2(u64 opcode, u64 retbuf[2], ...);
extern s64 beat_hcall3(u64 opcode, u64 retbuf[3], ...);
extern s64 beat_hcall4(u64 opcode, u64 retbuf[4], ...);
extern s64 beat_hcall5(u64 opcode, u64 retbuf[5], ...);
extern s64 beat_hcall6(u64 opcode, u64 retbuf[6], ...);

static inline s64 beat_downcount_of_interrupt(u64 plug_id)
{
	return beat_hcall_norets(HV_downcount_of_interrupt, plug_id);
}

static inline s64 beat_set_interrupt_mask(u64 index,
	u64 val0, u64 val1, u64 val2, u64 val3)
{
	return beat_hcall_norets(HV_set_interrupt_mask, index,
	       val0, val1, val2, val3);
}

static inline s64 beat_destruct_irq_plug(u64 plug_id)
{
	return beat_hcall_norets(HV_destruct_irq_plug, plug_id);
}

static inline s64 beat_construct_and_connect_irq_plug(u64 plug_id,
	u64 outlet_id)
{
	return beat_hcall_norets(HV_construct_and_connect_irq_plug, plug_id,
	       outlet_id);
}

static inline s64 beat_detect_pending_interrupts(u64 index, u64 *retbuf)
{
	return beat_hcall4(HV_detect_pending_interrupts, retbuf, index);
}

static inline s64 beat_pause(u64 style)
{
	return beat_hcall_norets(HV_pause, style);
}

static inline s64 beat_read_htab_entries(u64 htab_id, u64 index, u64 *retbuf)
{
	return beat_hcall5(HV_read_htab_entries, retbuf, htab_id, index);
}

static inline s64 beat_insert_htab_entry(u64 htab_id, u64 group,
	u64 bitmask, u64 hpte_v, u64 hpte_r, u64 *slot)
{
	u64 dummy[3];
	s64 ret;

	ret = beat_hcall3(HV_insert_htab_entry, dummy, htab_id, group,
		bitmask, hpte_v, hpte_r);
	*slot = dummy[0];
	return ret;
}

static inline s64 beat_write_htab_entry(u64 htab_id, u64 slot,
	u64 hpte_v, u64 hpte_r, u64 mask_v, u64 mask_r,
	u64 *ret_v, u64 *ret_r)
{
	u64 dummy[2];
	s64 ret;

	ret = beat_hcall2(HV_write_htab_entry, dummy, htab_id, slot,
		hpte_v, hpte_r, mask_v, mask_r);
	*ret_v = dummy[0];
	*ret_r = dummy[1];
	return ret;
}

static inline void beat_shutdown_logical_partition(u64 code)
{
	(void)beat_hcall_norets(HV_shutdown_logical_partition, code);
}

static inline s64 beat_rtc_write(u64 time_from_epoch)
{
	return beat_hcall_norets(HV_rtc_write, time_from_epoch);
}

static inline s64 beat_rtc_read(u64 *time_from_epoch)
{
	u64 dummy[1];
	s64 ret;

	ret = beat_hcall1(HV_rtc_read, dummy);
	*time_from_epoch = dummy[0];
	return ret;
}

#define	BEAT_NVRW_CNT	(sizeof(u64) * 6)

static inline s64 beat_eeprom_write(u64 index, u64 length, u8 *buffer)
{
	u64	b[6];

	if (length > BEAT_NVRW_CNT)
		return -1;
	memcpy(b, buffer, sizeof(b));
	return beat_hcall_norets8(HV_eeprom_write, index, length,
		b[0], b[1], b[2], b[3], b[4], b[5]);
}

static inline s64 beat_eeprom_read(u64 index, u64 length, u8 *buffer)
{
	u64	b[6];
	s64	ret;

	if (length > BEAT_NVRW_CNT)
		return -1;
	ret = beat_hcall6(HV_eeprom_read, b, index, length);
	memcpy(buffer, b, length);
	return ret;
}

static inline s64 beat_set_dabr(u64 value, u64 style)
{
	return beat_hcall_norets(HV_set_dabr, value, style);
}

static inline s64 beat_get_characters_from_console(u64 termno, u64 *len,
	u8 *buffer)
{
	u64 dummy[3];
	s64 ret;

	ret = beat_hcall3(HV_get_characters_from_console, dummy, termno, len);
	*len = dummy[0];
	memcpy(buffer, dummy + 1, *len);
	return ret;
}

static inline s64 beat_put_characters_to_console(u64 termno, u64 len,
	u8 *buffer)
{
	u64 b[2];

	memcpy(b, buffer, len);
	return beat_hcall_norets(HV_put_characters_to_console, termno, len,					 b[0], b[1]);
}

static inline s64 beat_get_spe_privileged_state_1_registers(
		u64 id, u64 offsetof, u64 *value)
{
	u64 dummy[1];
	s64 ret;

	ret = beat_hcall1(HV_get_spe_privileged_state_1_registers, dummy, id,
		offsetof);
	*value = dummy[0];
	return ret;
}

static inline s64 beat_set_irq_mask_for_spe(u64 id, u64 class, u64 mask)
{
	return beat_hcall_norets(HV_set_irq_mask_for_spe, id, class, mask);
}

static inline s64 beat_clear_interrupt_status_of_spe(u64 id, u64 class,
	u64 mask)
{
	return beat_hcall_norets(HV_clear_interrupt_status_of_spe,
		id, class, mask);
}

static inline s64 beat_set_spe_privileged_state_1_registers(
		u64 id, u64 offsetof, u64 value)
{
	return beat_hcall_norets(HV_set_spe_privileged_state_1_registers,
		id, offsetof, value);
}

static inline s64 beat_get_interrupt_status_of_spe(u64 id, u64 class, u64 *val)
{
	u64 dummy[1];
	s64 ret;

	ret = beat_hcall1(HV_get_interrupt_status_of_spe, dummy, id, class);
	*val = dummy[0];
	return ret;
}

static inline s64 beat_put_iopte(u64 ioas_id, u64 io_addr, u64 real_addr,
	u64 ioid, u64 flags)
{
	return beat_hcall_norets(HV_put_iopte, ioas_id, io_addr, real_addr,
		ioid, flags);
}

#endif
