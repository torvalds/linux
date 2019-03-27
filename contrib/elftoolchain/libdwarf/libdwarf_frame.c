/*-
 * Copyright (c) 2009-2011,2014 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "_libdwarf.h"

ELFTC_VCSID("$Id: libdwarf_frame.c 3589 2018-03-13 20:34:33Z kaiwang27 $");

static int
_dwarf_frame_find_cie(Dwarf_FrameSec fs, Dwarf_Unsigned offset,
    Dwarf_Cie *ret_cie)
{
	Dwarf_Cie cie;

	STAILQ_FOREACH(cie, &fs->fs_cielist, cie_next) {
		if (cie->cie_offset == offset)
			break;
	}

	if (cie == NULL)
		return (DW_DLE_NO_ENTRY);

	if (ret_cie != NULL)
		*ret_cie = cie;

	return (DW_DLE_NONE);
}

static int
_dwarf_frame_read_lsb_encoded(Dwarf_Debug dbg, Dwarf_Cie cie, uint64_t *val,
    uint8_t *data, uint64_t *offsetp, uint8_t encode, Dwarf_Addr pc,
    Dwarf_Error *error)
{
	uint8_t application;

	if (encode == DW_EH_PE_omit)
		return (DW_DLE_NONE);

	application = encode & 0xf0;
	encode &= 0x0f;

	switch (encode) {
	case DW_EH_PE_absptr:
		*val = dbg->read(data, offsetp, cie->cie_addrsize);
		break;
	case DW_EH_PE_uleb128:
		*val = _dwarf_read_uleb128(data, offsetp);
		break;
	case DW_EH_PE_udata2:
		*val = dbg->read(data, offsetp, 2);
		break;
	case DW_EH_PE_udata4:
		*val = dbg->read(data, offsetp, 4);
		break;
	case DW_EH_PE_udata8:
		*val = dbg->read(data, offsetp, 8);
		break;
	case DW_EH_PE_sleb128:
		*val = _dwarf_read_sleb128(data, offsetp);
		break;
	case DW_EH_PE_sdata2:
		*val = (int16_t) dbg->read(data, offsetp, 2);
		break;
	case DW_EH_PE_sdata4:
		*val = (int32_t) dbg->read(data, offsetp, 4);
		break;
	case DW_EH_PE_sdata8:
		*val = dbg->read(data, offsetp, 8);
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_FRAME_AUGMENTATION_UNKNOWN);
		return (DW_DLE_FRAME_AUGMENTATION_UNKNOWN);
	}

	if (application == DW_EH_PE_pcrel) {
		/*
		 * Value is relative to .eh_frame section virtual addr.
		 */
		switch (encode) {
		case DW_EH_PE_uleb128:
		case DW_EH_PE_udata2:
		case DW_EH_PE_udata4:
		case DW_EH_PE_udata8:
			*val += pc;
			break;
		case DW_EH_PE_sleb128:
		case DW_EH_PE_sdata2:
		case DW_EH_PE_sdata4:
		case DW_EH_PE_sdata8:
			*val = pc + (int64_t) *val;
			break;
		default:
			/* DW_EH_PE_absptr is absolute value. */
			break;
		}
	}

	/* XXX Applications other than DW_EH_PE_pcrel are not handled. */

	return (DW_DLE_NONE);
}

static int
_dwarf_frame_parse_lsb_cie_augment(Dwarf_Debug dbg, Dwarf_Cie cie,
    Dwarf_Error *error)
{
	uint8_t *aug_p, *augdata_p;
	uint64_t val, offset;
	uint8_t encode;
	int ret;

	assert(cie->cie_augment != NULL && *cie->cie_augment == 'z');

	/*
	 * Here we're only interested in the presence of augment 'R'
	 * and associated CIE augment data, which describes the
	 * encoding scheme of FDE PC begin and range.
	 */
	aug_p = &cie->cie_augment[1];
	augdata_p = cie->cie_augdata;
	while (*aug_p != '\0') {
		switch (*aug_p) {
		case 'S':
			break;
		case 'L':
			/* Skip one augment in augment data. */
			augdata_p++;
			break;
		case 'P':
			/* Skip two augments in augment data. */
			encode = *augdata_p++;
			offset = 0;
			ret = _dwarf_frame_read_lsb_encoded(dbg, cie, &val,
			    augdata_p, &offset, encode, 0, error);
			if (ret != DW_DLE_NONE)
				return (ret);
			augdata_p += offset;
			break;
		case 'R':
			cie->cie_fde_encode = *augdata_p++;
			break;
		default:
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_FRAME_AUGMENTATION_UNKNOWN);
			return (DW_DLE_FRAME_AUGMENTATION_UNKNOWN);
		}
		aug_p++;
	}

	return (DW_DLE_NONE);
}

static int
_dwarf_frame_add_cie(Dwarf_Debug dbg, Dwarf_FrameSec fs, Dwarf_Section *ds,
    Dwarf_Unsigned *off, Dwarf_Cie *ret_cie, Dwarf_Error *error)
{
	Dwarf_Cie cie;
	uint64_t length;
	int dwarf_size, ret;
	char *p;

	/* Check if we already added this CIE. */
	if (_dwarf_frame_find_cie(fs, *off, &cie) != DW_DLE_NO_ENTRY) {
		*off += cie->cie_length + 4;
		return (DW_DLE_NONE);
	}

	if ((cie = calloc(1, sizeof(struct _Dwarf_Cie))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INSERT_TAIL(&fs->fs_cielist, cie, cie_next);

	cie->cie_dbg = dbg;
	cie->cie_index = fs->fs_cielen;
	cie->cie_offset = *off;

	length = dbg->read(ds->ds_data, off, 4);
	if (length == 0xffffffff) {
		dwarf_size = 8;
		length = dbg->read(ds->ds_data, off, 8);
	} else
		dwarf_size = 4;

	if (length > ds->ds_size - *off) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_DEBUG_FRAME_LENGTH_BAD);
		return (DW_DLE_DEBUG_FRAME_LENGTH_BAD);
	}

	(void) dbg->read(ds->ds_data, off, dwarf_size); /* Skip CIE id. */
	cie->cie_length = length;

	cie->cie_version = dbg->read(ds->ds_data, off, 1);
	if (cie->cie_version != 1 && cie->cie_version != 3 &&
	    cie->cie_version != 4) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_FRAME_VERSION_BAD);
		return (DW_DLE_FRAME_VERSION_BAD);
	}

	cie->cie_augment = ds->ds_data + *off;
	p = (char *) ds->ds_data;
	while (p[(*off)++] != '\0')
		;

	/* We only recognize normal .dwarf_frame and GNU .eh_frame sections. */
	if (*cie->cie_augment != 0 && *cie->cie_augment != 'z') {
		*off = cie->cie_offset + ((dwarf_size == 4) ? 4 : 12) +
		    cie->cie_length;
		return (DW_DLE_NONE);
	}

	/* Optional EH Data field for .eh_frame section. */
	if (strstr((char *)cie->cie_augment, "eh") != NULL)
		cie->cie_ehdata = dbg->read(ds->ds_data, off,
		    dbg->dbg_pointer_size);

	/* DWARF4 added "address_size" and "segment_size". */
	if (cie->cie_version == 4) {
		cie->cie_addrsize = dbg->read(ds->ds_data, off, 1);
		cie->cie_segmentsize = dbg->read(ds->ds_data, off, 1);
	} else {
		/*
		 * Otherwise (DWARF[23]) we just set CIE addrsize to the
		 * debug context pointer size.
		 */
		cie->cie_addrsize = dbg->dbg_pointer_size;
	}

	cie->cie_caf = _dwarf_read_uleb128(ds->ds_data, off);
	cie->cie_daf = _dwarf_read_sleb128(ds->ds_data, off);

	/* Return address register. */
	if (cie->cie_version == 1)
		cie->cie_ra = dbg->read(ds->ds_data, off, 1);
	else
		cie->cie_ra = _dwarf_read_uleb128(ds->ds_data, off);

	/* Optional CIE augmentation data for .eh_frame section. */
	if (*cie->cie_augment == 'z') {
		cie->cie_auglen = _dwarf_read_uleb128(ds->ds_data, off);
		cie->cie_augdata = ds->ds_data + *off;
		*off += cie->cie_auglen;
		/*
		 * XXX Use DW_EH_PE_absptr for default FDE PC start/range,
		 * in case _dwarf_frame_parse_lsb_cie_augment fails to
		 * find out the real encode.
		 */
		cie->cie_fde_encode = DW_EH_PE_absptr;
		ret = _dwarf_frame_parse_lsb_cie_augment(dbg, cie, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	/* CIE Initial instructions. */
	cie->cie_initinst = ds->ds_data + *off;
	if (dwarf_size == 4)
		cie->cie_instlen = cie->cie_offset + 4 + length - *off;
	else
		cie->cie_instlen = cie->cie_offset + 12 + length - *off;

	*off += cie->cie_instlen;

#ifdef FRAME_DEBUG
	printf("cie:\n");
	printf("\tcie_version=%u cie_offset=%ju cie_length=%ju cie_augment=%s"
	    " cie_instlen=%ju cie->cie_caf=%ju cie->cie_daf=%jd off=%ju\n",
	    cie->cie_version, cie->cie_offset, cie->cie_length,
	    (char *)cie->cie_augment, cie->cie_instlen, cie->cie_caf,
	    cie->cie_daf, *off);
#endif

	if (ret_cie != NULL)
		*ret_cie = cie;

	fs->fs_cielen++;

	return (DW_DLE_NONE);
}

static int
_dwarf_frame_add_fde(Dwarf_Debug dbg, Dwarf_FrameSec fs, Dwarf_Section *ds,
    Dwarf_Unsigned *off, int eh_frame, Dwarf_Error *error)
{
	Dwarf_Cie cie;
	Dwarf_Fde fde;
	Dwarf_Unsigned cieoff;
	uint64_t length, val;
	int dwarf_size, ret;

	if ((fde = calloc(1, sizeof(struct _Dwarf_Fde))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INSERT_TAIL(&fs->fs_fdelist, fde, fde_next);

	fde->fde_dbg = dbg;
	fde->fde_fs = fs;
	fde->fde_addr = ds->ds_data + *off;
	fde->fde_offset = *off;

	length = dbg->read(ds->ds_data, off, 4);
	if (length == 0xffffffff) {
		dwarf_size = 8;
		length = dbg->read(ds->ds_data, off, 8);
	} else
		dwarf_size = 4;

	if (length > ds->ds_size - *off) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_DEBUG_FRAME_LENGTH_BAD);
		return (DW_DLE_DEBUG_FRAME_LENGTH_BAD);
	}

	fde->fde_length = length;

	if (eh_frame) {
		fde->fde_cieoff = dbg->read(ds->ds_data, off, 4);
		cieoff = *off - (4 + fde->fde_cieoff);
		/* This delta should never be 0. */
		if (cieoff == fde->fde_offset) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_NO_CIE_FOR_FDE);
			return (DW_DLE_NO_CIE_FOR_FDE);
		}
	} else {
		fde->fde_cieoff = dbg->read(ds->ds_data, off, dwarf_size);
		cieoff = fde->fde_cieoff;
	}

	if (_dwarf_frame_find_cie(fs, cieoff, &cie) ==
	    DW_DLE_NO_ENTRY) {
		ret = _dwarf_frame_add_cie(dbg, fs, ds, &cieoff, &cie,
		    error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}
	fde->fde_cie = cie;
	if (eh_frame) {
		/*
		 * The FDE PC start/range for .eh_frame is encoded according
		 * to the LSB spec's extension to DWARF2.
		 */
		ret = _dwarf_frame_read_lsb_encoded(dbg, cie, &val,
		    ds->ds_data, off, cie->cie_fde_encode, ds->ds_addr + *off,
		    error);
		if (ret != DW_DLE_NONE)
			return (ret);
		fde->fde_initloc = val;
		/*
		 * FDE PC range should not be relative value to anything.
		 * So pass 0 for pc value.
		 */
		ret = _dwarf_frame_read_lsb_encoded(dbg, cie, &val,
		    ds->ds_data, off, cie->cie_fde_encode, 0, error);
		if (ret != DW_DLE_NONE)
			return (ret);
		fde->fde_adrange = val;
	} else {
		fde->fde_initloc = dbg->read(ds->ds_data, off,
		    cie->cie_addrsize);
		fde->fde_adrange = dbg->read(ds->ds_data, off,
		    cie->cie_addrsize);
	}

	/* Optional FDE augmentation data for .eh_frame section. (ignored) */
	if (eh_frame && *cie->cie_augment == 'z') {
		fde->fde_auglen = _dwarf_read_uleb128(ds->ds_data, off);
		fde->fde_augdata = ds->ds_data + *off;
		*off += fde->fde_auglen;
	}

	fde->fde_inst = ds->ds_data + *off;
	if (dwarf_size == 4)
		fde->fde_instlen = fde->fde_offset + 4 + length - *off;
	else
		fde->fde_instlen = fde->fde_offset + 12 + length - *off;

	*off += fde->fde_instlen;

#ifdef FRAME_DEBUG
	printf("fde:");
	if (eh_frame)
		printf("(eh_frame)");
	putchar('\n');
	printf("\tfde_offset=%ju fde_length=%ju fde_cieoff=%ju"
	    " fde_instlen=%ju off=%ju\n", fde->fde_offset, fde->fde_length,
	    fde->fde_cieoff, fde->fde_instlen, *off);
#endif

	fs->fs_fdelen++;

	return (DW_DLE_NONE);
}

static void
_dwarf_frame_section_cleanup(Dwarf_FrameSec fs)
{
	Dwarf_Cie cie, tcie;
	Dwarf_Fde fde, tfde;

	STAILQ_FOREACH_SAFE(cie, &fs->fs_cielist, cie_next, tcie) {
		STAILQ_REMOVE(&fs->fs_cielist, cie, _Dwarf_Cie, cie_next);
		free(cie);
	}

	STAILQ_FOREACH_SAFE(fde, &fs->fs_fdelist, fde_next, tfde) {
		STAILQ_REMOVE(&fs->fs_fdelist, fde, _Dwarf_Fde, fde_next);
		free(fde);
	}

	if (fs->fs_ciearray != NULL)
		free(fs->fs_ciearray);
	if (fs->fs_fdearray != NULL)
		free(fs->fs_fdearray);

	free(fs);
}

static int
_dwarf_frame_section_init(Dwarf_Debug dbg, Dwarf_FrameSec *frame_sec,
    Dwarf_Section *ds, int eh_frame, Dwarf_Error *error)
{
	Dwarf_FrameSec fs;
	Dwarf_Cie cie;
	Dwarf_Fde fde;
	uint64_t length, offset, cie_id, entry_off;
	int dwarf_size, i, ret;

	assert(frame_sec != NULL);
	assert(*frame_sec == NULL);

	if ((fs = calloc(1, sizeof(struct _Dwarf_FrameSec))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INIT(&fs->fs_cielist);
	STAILQ_INIT(&fs->fs_fdelist);

	offset = 0;
	while (offset < ds->ds_size) {
		entry_off = offset;
		length = dbg->read(ds->ds_data, &offset, 4);
		if (length == 0xffffffff) {
			dwarf_size = 8;
			length = dbg->read(ds->ds_data, &offset, 8);
		} else
			dwarf_size = 4;

		if (length > ds->ds_size - offset ||
		    (length == 0 && !eh_frame)) {
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_DEBUG_FRAME_LENGTH_BAD);
			return (DW_DLE_DEBUG_FRAME_LENGTH_BAD);
		}

		/* Check terminator for .eh_frame */
		if (eh_frame && length == 0)
			break;

		cie_id = dbg->read(ds->ds_data, &offset, dwarf_size);

		if (eh_frame) {
			/* GNU .eh_frame use CIE id 0. */
			if (cie_id == 0)
				ret = _dwarf_frame_add_cie(dbg, fs, ds,
				    &entry_off, NULL, error);
			else
				ret = _dwarf_frame_add_fde(dbg, fs, ds,
				    &entry_off, 1, error);
		} else {
			/* .dwarf_frame use CIE id ~0 */
			if ((dwarf_size == 4 && cie_id == ~0U) ||
			    (dwarf_size == 8 && cie_id == ~0ULL))
				ret = _dwarf_frame_add_cie(dbg, fs, ds,
				    &entry_off, NULL, error);
			else
				ret = _dwarf_frame_add_fde(dbg, fs, ds,
				    &entry_off, 0, error);
		}

		if (ret != DW_DLE_NONE)
			goto fail_cleanup;

		offset = entry_off;
	}

	/* Create CIE array. */
	if (fs->fs_cielen > 0) {
		if ((fs->fs_ciearray = malloc(sizeof(Dwarf_Cie) *
		    fs->fs_cielen)) == NULL) {
			ret = DW_DLE_MEMORY;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}
		i = 0;
		STAILQ_FOREACH(cie, &fs->fs_cielist, cie_next) {
			fs->fs_ciearray[i++] = cie;
		}
		assert((Dwarf_Unsigned)i == fs->fs_cielen);
	}

	/* Create FDE array. */
	if (fs->fs_fdelen > 0) {
		if ((fs->fs_fdearray = malloc(sizeof(Dwarf_Fde) *
		    fs->fs_fdelen)) == NULL) {
			ret = DW_DLE_MEMORY;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}
		i = 0;
		STAILQ_FOREACH(fde, &fs->fs_fdelist, fde_next) {
			fs->fs_fdearray[i++] = fde;
		}
		assert((Dwarf_Unsigned)i == fs->fs_fdelen);
	}

	*frame_sec = fs;

	return (DW_DLE_NONE);

fail_cleanup:

	_dwarf_frame_section_cleanup(fs);

	return (ret);
}

static int
_dwarf_frame_run_inst(Dwarf_Debug dbg, Dwarf_Regtable3 *rt, uint8_t addr_size,
    uint8_t *insts, Dwarf_Unsigned len, Dwarf_Unsigned caf, Dwarf_Signed daf,
    Dwarf_Addr pc, Dwarf_Addr pc_req, Dwarf_Addr *row_pc, Dwarf_Error *error)
{
	Dwarf_Regtable3 *init_rt, *saved_rt;
	uint8_t *p, *pe;
	uint8_t high2, low6;
	uint64_t reg, reg2, uoff, soff;
	int ret;

#define	CFA	rt->rt3_cfa_rule
#define	INITCFA	init_rt->rt3_cfa_rule
#define	RL	rt->rt3_rules
#define	INITRL	init_rt->rt3_rules

#define CHECK_TABLE_SIZE(x)						\
	do {								\
		if ((x) >= rt->rt3_reg_table_size) {			\
			DWARF_SET_ERROR(dbg, error,			\
			    DW_DLE_DF_REG_NUM_TOO_HIGH);		\
			ret = DW_DLE_DF_REG_NUM_TOO_HIGH;		\
			goto program_done;				\
		}							\
	} while(0)

#ifdef FRAME_DEBUG
	printf("frame_run_inst: (caf=%ju, daf=%jd)\n", caf, daf);
#endif

	ret = DW_DLE_NONE;
	init_rt = saved_rt = NULL;
	*row_pc = pc;

	/* Save a copy of the table as initial state. */
	_dwarf_frame_regtable_copy(dbg, &init_rt, rt, error);

	p = insts;
	pe = p + len;

	while (p < pe) {

#ifdef FRAME_DEBUG
		printf("p=%p pe=%p pc=%#jx pc_req=%#jx\n", p, pe, pc, pc_req);
#endif

		if (*p == DW_CFA_nop) {
#ifdef FRAME_DEBUG
			printf("DW_CFA_nop\n");
#endif
			p++;
			continue;
		}

		high2 = *p & 0xc0;
		low6 = *p & 0x3f;
		p++;

		if (high2 > 0) {
			switch (high2) {
			case DW_CFA_advance_loc:
				pc += low6 * caf;
#ifdef FRAME_DEBUG
				printf("DW_CFA_advance_loc(%#jx(%u))\n", pc,
				    low6);
#endif
				if (pc_req < pc)
					goto program_done;
				break;
			case DW_CFA_offset:
				*row_pc = pc;
				CHECK_TABLE_SIZE(low6);
				RL[low6].dw_offset_relevant = 1;
				RL[low6].dw_value_type = DW_EXPR_OFFSET;
				RL[low6].dw_regnum = dbg->dbg_frame_cfa_value;
				RL[low6].dw_offset_or_block_len =
				    _dwarf_decode_uleb128(&p) * daf;
#ifdef FRAME_DEBUG
				printf("DW_CFA_offset(%jd)\n",
				    RL[low6].dw_offset_or_block_len);
#endif
				break;
			case DW_CFA_restore:
				*row_pc = pc;
				CHECK_TABLE_SIZE(low6);
				memcpy(&RL[low6], &INITRL[low6],
				    sizeof(Dwarf_Regtable_Entry3));
#ifdef FRAME_DEBUG
				printf("DW_CFA_restore(%u)\n", low6);
#endif
				break;
			default:
				DWARF_SET_ERROR(dbg, error,
				    DW_DLE_FRAME_INSTR_EXEC_ERROR);
				ret = DW_DLE_FRAME_INSTR_EXEC_ERROR;
				goto program_done;
			}

			continue;
		}

		switch (low6) {
		case DW_CFA_set_loc:
			pc = dbg->decode(&p, addr_size);
#ifdef FRAME_DEBUG
			printf("DW_CFA_set_loc(pc=%#jx)\n", pc);
#endif
			if (pc_req < pc)
				goto program_done;
			break;
		case DW_CFA_advance_loc1:
			pc += dbg->decode(&p, 1) * caf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_set_loc1(pc=%#jx)\n", pc);
#endif
			if (pc_req < pc)
				goto program_done;
			break;
		case DW_CFA_advance_loc2:
			pc += dbg->decode(&p, 2) * caf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_set_loc2(pc=%#jx)\n", pc);
#endif
			if (pc_req < pc)
				goto program_done;
			break;
		case DW_CFA_advance_loc4:
			pc += dbg->decode(&p, 4) * caf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_set_loc4(pc=%#jx)\n", pc);
#endif
			if (pc_req < pc)
				goto program_done;
			break;
		case DW_CFA_offset_extended:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			uoff = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 1;
			RL[reg].dw_value_type = DW_EXPR_OFFSET;
			RL[reg].dw_regnum = dbg->dbg_frame_cfa_value;
			RL[reg].dw_offset_or_block_len = uoff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_offset_extended(reg=%ju,uoff=%ju)\n",
			    reg, uoff);
#endif
			break;
		case DW_CFA_restore_extended:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			memcpy(&RL[reg], &INITRL[reg],
			    sizeof(Dwarf_Regtable_Entry3));
#ifdef FRAME_DEBUG
			printf("DW_CFA_restore_extended(%ju)\n", reg);
#endif
			break;
		case DW_CFA_undefined:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 0;
			RL[reg].dw_regnum = dbg->dbg_frame_undefined_value;
#ifdef FRAME_DEBUG
			printf("DW_CFA_undefined(%ju)\n", reg);
#endif
			break;
		case DW_CFA_same_value:
			reg = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 0;
			RL[reg].dw_regnum = dbg->dbg_frame_same_value;
#ifdef FRAME_DEBUG
			printf("DW_CFA_same_value(%ju)\n", reg);
#endif
			break;
		case DW_CFA_register:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			reg2 = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 0;
			RL[reg].dw_regnum = reg2;
#ifdef FRAME_DEBUG
			printf("DW_CFA_register(reg=%ju,reg2=%ju)\n", reg,
			    reg2);
#endif
			break;
		case DW_CFA_remember_state:
			_dwarf_frame_regtable_copy(dbg, &saved_rt, rt, error);
#ifdef FRAME_DEBUG
			printf("DW_CFA_remember_state\n");
#endif
			break;
		case DW_CFA_restore_state:
			*row_pc = pc;
			_dwarf_frame_regtable_copy(dbg, &rt, saved_rt, error);
#ifdef FRAME_DEBUG
			printf("DW_CFA_restore_state\n");
#endif
			break;
		case DW_CFA_def_cfa:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			uoff = _dwarf_decode_uleb128(&p);
			CFA.dw_offset_relevant = 1;
			CFA.dw_value_type = DW_EXPR_OFFSET;
			CFA.dw_regnum = reg;
			CFA.dw_offset_or_block_len = uoff;
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa(reg=%ju,uoff=%ju)\n", reg, uoff);
#endif
			break;
		case DW_CFA_def_cfa_register:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			CFA.dw_regnum = reg;
			/*
			 * Note that DW_CFA_def_cfa_register change the CFA
			 * rule register while keep the old offset. So we
			 * should not touch the CFA.dw_offset_relevant flag
			 * here.
			 */
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa_register(%ju)\n", reg);
#endif
			break;
		case DW_CFA_def_cfa_offset:
			*row_pc = pc;
			uoff = _dwarf_decode_uleb128(&p);
			CFA.dw_offset_relevant = 1;
			CFA.dw_value_type = DW_EXPR_OFFSET;
			CFA.dw_offset_or_block_len = uoff;
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa_offset(%ju)\n", uoff);
#endif
			break;
		case DW_CFA_def_cfa_expression:
			*row_pc = pc;
			CFA.dw_offset_relevant = 0;
			CFA.dw_value_type = DW_EXPR_EXPRESSION;
			CFA.dw_offset_or_block_len = _dwarf_decode_uleb128(&p);
			CFA.dw_block_ptr = p;
			p += CFA.dw_offset_or_block_len;
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa_expression\n");
#endif
			break;
		case DW_CFA_expression:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 0;
			RL[reg].dw_value_type = DW_EXPR_EXPRESSION;
			RL[reg].dw_offset_or_block_len =
			    _dwarf_decode_uleb128(&p);
			RL[reg].dw_block_ptr = p;
			p += RL[reg].dw_offset_or_block_len;
#ifdef FRAME_DEBUG
			printf("DW_CFA_expression\n");
#endif
			break;
		case DW_CFA_offset_extended_sf:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			soff = _dwarf_decode_sleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 1;
			RL[reg].dw_value_type = DW_EXPR_OFFSET;
			RL[reg].dw_regnum = dbg->dbg_frame_cfa_value;
			RL[reg].dw_offset_or_block_len = soff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_offset_extended_sf(reg=%ju,soff=%jd)\n",
			    reg, soff);
#endif
			break;
		case DW_CFA_def_cfa_sf:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			soff = _dwarf_decode_sleb128(&p);
			CFA.dw_offset_relevant = 1;
			CFA.dw_value_type = DW_EXPR_OFFSET;
			CFA.dw_regnum = reg;
			CFA.dw_offset_or_block_len = soff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa_sf(reg=%ju,soff=%jd)\n", reg,
			    soff);
#endif
			break;
		case DW_CFA_def_cfa_offset_sf:
			*row_pc = pc;
			soff = _dwarf_decode_sleb128(&p);
			CFA.dw_offset_relevant = 1;
			CFA.dw_value_type = DW_EXPR_OFFSET;
			CFA.dw_offset_or_block_len = soff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_def_cfa_offset_sf(soff=%jd)\n", soff);
#endif
			break;
		case DW_CFA_val_offset:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			uoff = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 1;
			RL[reg].dw_value_type = DW_EXPR_VAL_OFFSET;
			RL[reg].dw_regnum = dbg->dbg_frame_cfa_value;
			RL[reg].dw_offset_or_block_len = uoff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_val_offset(reg=%ju,uoff=%ju)\n", reg,
			    uoff);
#endif
			break;
		case DW_CFA_val_offset_sf:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			soff = _dwarf_decode_sleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 1;
			RL[reg].dw_value_type = DW_EXPR_VAL_OFFSET;
			RL[reg].dw_regnum = dbg->dbg_frame_cfa_value;
			RL[reg].dw_offset_or_block_len = soff * daf;
#ifdef FRAME_DEBUG
			printf("DW_CFA_val_offset_sf(reg=%ju,soff=%jd)\n", reg,
			    soff);
#endif
			break;
		case DW_CFA_val_expression:
			*row_pc = pc;
			reg = _dwarf_decode_uleb128(&p);
			CHECK_TABLE_SIZE(reg);
			RL[reg].dw_offset_relevant = 0;
			RL[reg].dw_value_type = DW_EXPR_VAL_EXPRESSION;
			RL[reg].dw_offset_or_block_len =
			    _dwarf_decode_uleb128(&p);
			RL[reg].dw_block_ptr = p;
			p += RL[reg].dw_offset_or_block_len;
#ifdef FRAME_DEBUG
			printf("DW_CFA_val_expression\n");
#endif
			break;
		default:
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_FRAME_INSTR_EXEC_ERROR);
			ret = DW_DLE_FRAME_INSTR_EXEC_ERROR;
			goto program_done;
		}
	}

program_done:

	free(init_rt->rt3_rules);
	free(init_rt);
	if (saved_rt) {
		free(saved_rt->rt3_rules);
		free(saved_rt);
	}

	return (ret);

#undef	CFA
#undef	INITCFA
#undef	RL
#undef	INITRL
#undef	CHECK_TABLE_SIZE
}

static int
_dwarf_frame_convert_inst(Dwarf_Debug dbg, uint8_t addr_size, uint8_t *insts,
    Dwarf_Unsigned len, Dwarf_Unsigned *count, Dwarf_Frame_Op *fop,
    Dwarf_Frame_Op3 *fop3, Dwarf_Error *error)
{
	uint8_t *p, *pe;
	uint8_t high2, low6;
	uint64_t reg, reg2, uoff, soff, blen;

#define	SET_BASE_OP(x)						\
	do {							\
		if (fop != NULL)				\
			fop[*count].fp_base_op = (x) >> 6;	\
		if (fop3 != NULL)				\
			fop3[*count].fp_base_op = (x) >> 6;	\
	} while(0)

#define	SET_EXTENDED_OP(x)					\
	do {							\
		if (fop != NULL)				\
			fop[*count].fp_extended_op = (x);	\
		if (fop3 != NULL)				\
			fop3[*count].fp_extended_op = (x);	\
	} while(0)

#define	SET_REGISTER(x)						\
	do {							\
		if (fop != NULL)				\
			fop[*count].fp_register = (x);		\
		if (fop3 != NULL)				\
			fop3[*count].fp_register = (x);		\
	} while(0)

#define	SET_OFFSET(x)						\
	do {							\
		if (fop != NULL)				\
			fop[*count].fp_offset = (x);		\
		if (fop3 != NULL)				\
			fop3[*count].fp_offset_or_block_len =	\
			    (x);				\
	} while(0)

#define	SET_INSTR_OFFSET(x)					\
	do {							\
		if (fop != NULL)				\
			fop[*count].fp_instr_offset = (x);	\
		if (fop3 != NULL)				\
			fop3[*count].fp_instr_offset = (x);	\
	} while(0)

#define	SET_BLOCK_LEN(x)					\
	do {							\
		if (fop3 != NULL)				\
			fop3[*count].fp_offset_or_block_len =	\
			    (x);				\
	} while(0)

#define	SET_EXPR_BLOCK(addr, len)					\
	do {								\
		if (fop3 != NULL) {					\
			fop3[*count].fp_expr_block =			\
			    malloc((size_t) (len));			\
			if (fop3[*count].fp_expr_block == NULL)	{	\
				DWARF_SET_ERROR(dbg, error,		\
				    DW_DLE_MEMORY);			\
				return (DW_DLE_MEMORY);			\
			}						\
			memcpy(&fop3[*count].fp_expr_block,		\
			    (addr), (len));				\
		}							\
	} while(0)

	*count = 0;

	p = insts;
	pe = p + len;

	while (p < pe) {

		SET_INSTR_OFFSET(p - insts);

		if (*p == DW_CFA_nop) {
			p++;
			(*count)++;
			continue;
		}

		high2 = *p & 0xc0;
		low6 = *p & 0x3f;
		p++;

		if (high2 > 0) {
			switch (high2) {
			case DW_CFA_advance_loc:
				SET_BASE_OP(high2);
				SET_OFFSET(low6);
				break;
			case DW_CFA_offset:
				SET_BASE_OP(high2);
				SET_REGISTER(low6);
				uoff = _dwarf_decode_uleb128(&p);
				SET_OFFSET(uoff);
				break;
			case DW_CFA_restore:
				SET_BASE_OP(high2);
				SET_REGISTER(low6);
				break;
			default:
				DWARF_SET_ERROR(dbg, error,
				    DW_DLE_FRAME_INSTR_EXEC_ERROR);
				return (DW_DLE_FRAME_INSTR_EXEC_ERROR);
			}

			(*count)++;
			continue;
		}

		SET_EXTENDED_OP(low6);

		switch (low6) {
		case DW_CFA_set_loc:
			uoff = dbg->decode(&p, addr_size);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_advance_loc1:
			uoff = dbg->decode(&p, 1);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_advance_loc2:
			uoff = dbg->decode(&p, 2);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_advance_loc4:
			uoff = dbg->decode(&p, 4);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_offset_extended:
		case DW_CFA_def_cfa:
		case DW_CFA_val_offset:
			reg = _dwarf_decode_uleb128(&p);
			uoff = _dwarf_decode_uleb128(&p);
			SET_REGISTER(reg);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_restore_extended:
		case DW_CFA_undefined:
		case DW_CFA_same_value:
		case DW_CFA_def_cfa_register:
			reg = _dwarf_decode_uleb128(&p);
			SET_REGISTER(reg);
			break;
		case DW_CFA_register:
			reg = _dwarf_decode_uleb128(&p);
			reg2 = _dwarf_decode_uleb128(&p);
			SET_REGISTER(reg);
			SET_OFFSET(reg2);
			break;
		case DW_CFA_remember_state:
		case DW_CFA_restore_state:
			break;
		case DW_CFA_def_cfa_offset:
			uoff = _dwarf_decode_uleb128(&p);
			SET_OFFSET(uoff);
			break;
		case DW_CFA_def_cfa_expression:
			blen = _dwarf_decode_uleb128(&p);
			SET_BLOCK_LEN(blen);
			SET_EXPR_BLOCK(p, blen);
			p += blen;
			break;
		case DW_CFA_expression:
		case DW_CFA_val_expression:
			reg = _dwarf_decode_uleb128(&p);
			blen = _dwarf_decode_uleb128(&p);
			SET_REGISTER(reg);
			SET_BLOCK_LEN(blen);
			SET_EXPR_BLOCK(p, blen);
			p += blen;
			break;
		case DW_CFA_offset_extended_sf:
		case DW_CFA_def_cfa_sf:
		case DW_CFA_val_offset_sf:
			reg = _dwarf_decode_uleb128(&p);
			soff = _dwarf_decode_sleb128(&p);
			SET_REGISTER(reg);
			SET_OFFSET(soff);
			break;
		case DW_CFA_def_cfa_offset_sf:
			soff = _dwarf_decode_sleb128(&p);
			SET_OFFSET(soff);
			break;
		default:
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_FRAME_INSTR_EXEC_ERROR);
			return (DW_DLE_FRAME_INSTR_EXEC_ERROR);
		}

		(*count)++;
	}

	return (DW_DLE_NONE);
}

int
_dwarf_frame_get_fop(Dwarf_Debug dbg, uint8_t addr_size, uint8_t *insts,
    Dwarf_Unsigned len, Dwarf_Frame_Op **ret_oplist, Dwarf_Signed *ret_opcnt,
    Dwarf_Error *error)
{
	Dwarf_Frame_Op *oplist;
	Dwarf_Unsigned count;
	int ret;

	ret = _dwarf_frame_convert_inst(dbg, addr_size, insts, len, &count,
	    NULL, NULL, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	if ((oplist = calloc(count, sizeof(Dwarf_Frame_Op))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	ret = _dwarf_frame_convert_inst(dbg, addr_size, insts, len, &count,
	    oplist, NULL, error);
	if (ret != DW_DLE_NONE) {
		free(oplist);
		return (ret);
	}

	*ret_oplist = oplist;
	*ret_opcnt = count;

	return (DW_DLE_NONE);
}

int
_dwarf_frame_regtable_copy(Dwarf_Debug dbg, Dwarf_Regtable3 **dest,
    Dwarf_Regtable3 *src, Dwarf_Error *error)
{
	int i;

	assert(dest != NULL);
	assert(src != NULL);

	if (*dest == NULL) {
		if ((*dest = malloc(sizeof(Dwarf_Regtable3))) == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
		(*dest)->rt3_reg_table_size = src->rt3_reg_table_size;
		(*dest)->rt3_rules = malloc(src->rt3_reg_table_size *
		    sizeof(Dwarf_Regtable_Entry3));
		if ((*dest)->rt3_rules == NULL) {
			free(*dest);
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
	}

	memcpy(&(*dest)->rt3_cfa_rule, &src->rt3_cfa_rule,
	    sizeof(Dwarf_Regtable_Entry3));

	for (i = 0; i < (*dest)->rt3_reg_table_size &&
	     i < src->rt3_reg_table_size; i++)
		memcpy(&(*dest)->rt3_rules[i], &src->rt3_rules[i],
		    sizeof(Dwarf_Regtable_Entry3));

	for (; i < (*dest)->rt3_reg_table_size; i++)
		(*dest)->rt3_rules[i].dw_regnum =
		    dbg->dbg_frame_undefined_value;

	return (DW_DLE_NONE);
}

int
_dwarf_frame_get_internal_table(Dwarf_Fde fde, Dwarf_Addr pc_req,
    Dwarf_Regtable3 **ret_rt, Dwarf_Addr *ret_row_pc, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_Cie cie;
	Dwarf_Regtable3 *rt;
	Dwarf_Addr row_pc;
	int i, ret;

	assert(ret_rt != NULL);

	dbg = fde->fde_dbg;
	assert(dbg != NULL);

	rt = dbg->dbg_internal_reg_table;

	/* Clear the content of regtable from previous run. */
	memset(&rt->rt3_cfa_rule, 0, sizeof(Dwarf_Regtable_Entry3));
	memset(rt->rt3_rules, 0, rt->rt3_reg_table_size *
	    sizeof(Dwarf_Regtable_Entry3));

	/* Set rules to initial values. */
	for (i = 0; i < rt->rt3_reg_table_size; i++)
		rt->rt3_rules[i].dw_regnum = dbg->dbg_frame_rule_initial_value;

	/* Run initial instructions in CIE. */
	cie = fde->fde_cie;
	assert(cie != NULL);
	ret = _dwarf_frame_run_inst(dbg, rt, cie->cie_addrsize,
	    cie->cie_initinst, cie->cie_instlen, cie->cie_caf, cie->cie_daf, 0,
	    ~0ULL, &row_pc, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	/* Run instructions in FDE. */
	if (pc_req >= fde->fde_initloc) {
		ret = _dwarf_frame_run_inst(dbg, rt, cie->cie_addrsize,
		    fde->fde_inst, fde->fde_instlen, cie->cie_caf,
		    cie->cie_daf, fde->fde_initloc, pc_req, &row_pc, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	*ret_rt = rt;
	*ret_row_pc = row_pc;

	return (DW_DLE_NONE);
}

void
_dwarf_frame_cleanup(Dwarf_Debug dbg)
{
	Dwarf_Regtable3 *rt;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_READ);

	if (dbg->dbg_internal_reg_table) {
		rt = dbg->dbg_internal_reg_table;
		free(rt->rt3_rules);
		free(rt);
		dbg->dbg_internal_reg_table = NULL;
	}

	if (dbg->dbg_frame) {
		_dwarf_frame_section_cleanup(dbg->dbg_frame);
		dbg->dbg_frame = NULL;
	}

	if (dbg->dbg_eh_frame) {
		_dwarf_frame_section_cleanup(dbg->dbg_eh_frame);
		dbg->dbg_eh_frame = NULL;
	}
}

int
_dwarf_frame_section_load(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_Section *ds;

	if ((ds = _dwarf_find_section(dbg, ".debug_frame")) != NULL) {
		return (_dwarf_frame_section_init(dbg, &dbg->dbg_frame,
		    ds, 0, error));
	}

	return (DW_DLE_NONE);
}

int
_dwarf_frame_section_load_eh(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_Section *ds;

	if ((ds = _dwarf_find_section(dbg, ".eh_frame")) != NULL) {
		return (_dwarf_frame_section_init(dbg, &dbg->dbg_eh_frame,
		    ds, 1, error));
	}

	return (DW_DLE_NONE);
}

void
_dwarf_frame_params_init(Dwarf_Debug dbg)
{

	/* Initialise call frame related parameters. */
	dbg->dbg_frame_rule_table_size = DW_FRAME_LAST_REG_NUM;
	dbg->dbg_frame_rule_initial_value = DW_FRAME_REG_INITIAL_VALUE;
	dbg->dbg_frame_cfa_value = DW_FRAME_CFA_COL3;
	dbg->dbg_frame_same_value = DW_FRAME_SAME_VAL;
	dbg->dbg_frame_undefined_value = DW_FRAME_UNDEFINED_VAL;
}

int
_dwarf_frame_interal_table_init(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_Regtable3 *rt;

	if (dbg->dbg_internal_reg_table != NULL)
		return (DW_DLE_NONE);

	/* Initialise internal register table. */
	if ((rt = calloc(1, sizeof(Dwarf_Regtable3))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	rt->rt3_reg_table_size = dbg->dbg_frame_rule_table_size;
	if ((rt->rt3_rules = calloc(rt->rt3_reg_table_size,
	    sizeof(Dwarf_Regtable_Entry3))) == NULL) {
		free(rt);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	dbg->dbg_internal_reg_table = rt;

	return (DW_DLE_NONE);
}

#define	_FDE_INST_INIT_SIZE	128

int
_dwarf_frame_fde_add_inst(Dwarf_P_Fde fde, Dwarf_Small op, Dwarf_Unsigned val1,
    Dwarf_Unsigned val2, Dwarf_Error *error)
{
	Dwarf_P_Debug dbg;
	uint8_t high2, low6;
	int ret;

#define	ds	fde
#define	ds_data	fde_inst
#define	ds_cap	fde_instcap
#define	ds_size	fde_instlen

	assert(fde != NULL && fde->fde_dbg != NULL);
	dbg = fde->fde_dbg;

	if (fde->fde_inst == NULL) {
		fde->fde_instcap = _FDE_INST_INIT_SIZE;
		fde->fde_instlen = 0;
		if ((fde->fde_inst = malloc((size_t) fde->fde_instcap)) ==
		    NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
	}
	assert(fde->fde_instcap != 0);

	RCHECK(WRITE_VALUE(op, 1));
	if (op == DW_CFA_nop)
		return (DW_DLE_NONE);

	high2 = op & 0xc0;
	low6 = op & 0x3f;

	if (high2 > 0) {
		switch (high2) {
		case DW_CFA_advance_loc:
		case DW_CFA_restore:
			break;
		case DW_CFA_offset:
			RCHECK(WRITE_ULEB128(val1));
			break;
		default:
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_FRAME_INSTR_EXEC_ERROR);
			return (DW_DLE_FRAME_INSTR_EXEC_ERROR);
		}
		return (DW_DLE_NONE);
	}

	switch (low6) {
	case DW_CFA_set_loc:
		RCHECK(WRITE_VALUE(val1, dbg->dbg_pointer_size));
		break;
	case DW_CFA_advance_loc1:
		RCHECK(WRITE_VALUE(val1, 1));
		break;
	case DW_CFA_advance_loc2:
		RCHECK(WRITE_VALUE(val1, 2));
		break;
	case DW_CFA_advance_loc4:
		RCHECK(WRITE_VALUE(val1, 4));
		break;
	case DW_CFA_offset_extended:
	case DW_CFA_def_cfa:
	case DW_CFA_register:
		RCHECK(WRITE_ULEB128(val1));
		RCHECK(WRITE_ULEB128(val2));
		break;
	case DW_CFA_restore_extended:
	case DW_CFA_undefined:
	case DW_CFA_same_value:
	case DW_CFA_def_cfa_register:
	case DW_CFA_def_cfa_offset:
		RCHECK(WRITE_ULEB128(val1));
		break;
	case DW_CFA_remember_state:
	case DW_CFA_restore_state:
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_FRAME_INSTR_EXEC_ERROR);
		return (DW_DLE_FRAME_INSTR_EXEC_ERROR);
	}

	return (DW_DLE_NONE);

gen_fail:
	return (ret);

#undef	ds
#undef	ds_data
#undef	ds_cap
#undef	ds_size
}

static int
_dwarf_frame_gen_cie(Dwarf_P_Debug dbg, Dwarf_P_Section ds, Dwarf_P_Cie cie,
    Dwarf_Error *error)
{
	Dwarf_Unsigned len;
	uint64_t offset;
	int ret;

	assert(dbg != NULL && ds != NULL && cie != NULL);

	cie->cie_offset = offset = ds->ds_size;
	cie->cie_length = 0;
	cie->cie_version = 1;

	/* Length placeholder. */
	RCHECK(WRITE_VALUE(cie->cie_length, 4));

	/* .debug_frame use CIE id ~0. */
	RCHECK(WRITE_VALUE(~0U, 4));

	/* .debug_frame version is 1. (DWARF2) */
	RCHECK(WRITE_VALUE(cie->cie_version, 1));

	/* Write augmentation, if present. */
	if (cie->cie_augment != NULL)
		RCHECK(WRITE_BLOCK(cie->cie_augment,
		    strlen((char *) cie->cie_augment) + 1));
	else
		RCHECK(WRITE_VALUE(0, 1));

	/* Write caf, daf and ra. */
	RCHECK(WRITE_ULEB128(cie->cie_caf));
	RCHECK(WRITE_SLEB128(cie->cie_daf));
	RCHECK(WRITE_VALUE(cie->cie_ra, 1));

	/* Write initial instructions, if present. */
	if (cie->cie_initinst != NULL)
		RCHECK(WRITE_BLOCK(cie->cie_initinst, cie->cie_instlen));

	/* Add padding. */
	len = ds->ds_size - cie->cie_offset - 4;
	cie->cie_length = roundup(len, dbg->dbg_pointer_size);
	while (len++ < cie->cie_length)
		RCHECK(WRITE_VALUE(DW_CFA_nop, 1));

	/* Fill in the length field. */
	dbg->write(ds->ds_data, &offset, cie->cie_length, 4);
	
	return (DW_DLE_NONE);

gen_fail:
	return (ret);
}

static int
_dwarf_frame_gen_fde(Dwarf_P_Debug dbg, Dwarf_P_Section ds,
    Dwarf_Rel_Section drs, Dwarf_P_Fde fde, Dwarf_Error *error)
{
	Dwarf_Unsigned len;
	uint64_t offset;
	int ret;

	assert(dbg != NULL && ds != NULL && drs != NULL);
	assert(fde != NULL && fde->fde_cie != NULL);

	fde->fde_offset = offset = ds->ds_size;
	fde->fde_length = 0;
	fde->fde_cieoff = fde->fde_cie->cie_offset;

	/* Length placeholder. */
	RCHECK(WRITE_VALUE(fde->fde_length, 4));

	/* Write CIE pointer. */
	RCHECK(_dwarf_reloc_entry_add(dbg, drs, ds, dwarf_drt_data_reloc, 4,
	    ds->ds_size, 0, fde->fde_cieoff, ".debug_frame", error));

	/* Write FDE initial location. */
	RCHECK(_dwarf_reloc_entry_add(dbg, drs, ds, dwarf_drt_data_reloc,
	    dbg->dbg_pointer_size, ds->ds_size, fde->fde_symndx,
	    fde->fde_initloc, NULL, error));

	/*
	 * Write FDE address range. Use a pair of relocation entries if
	 * application provided end symbol index. Otherwise write the
	 * length without assoicating any relocation info.
	 */
	if (fde->fde_esymndx > 0)
		RCHECK(_dwarf_reloc_entry_add_pair(dbg, drs, ds,
		    dbg->dbg_pointer_size, ds->ds_size, fde->fde_symndx,
		    fde->fde_esymndx, fde->fde_initloc, fde->fde_eoff, error));
	else
		RCHECK(WRITE_VALUE(fde->fde_adrange, dbg->dbg_pointer_size));

	/* Write FDE frame instructions. */
	RCHECK(WRITE_BLOCK(fde->fde_inst, fde->fde_instlen));

	/* Add padding. */
	len = ds->ds_size - fde->fde_offset - 4;
	fde->fde_length = roundup(len, dbg->dbg_pointer_size);
	while (len++ < fde->fde_length)
		RCHECK(WRITE_VALUE(DW_CFA_nop, 1));

	/* Fill in the length field. */
	dbg->write(ds->ds_data, &offset, fde->fde_length, 4);

	return (DW_DLE_NONE);

gen_fail:
	return (ret);
}

int
_dwarf_frame_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Rel_Section drs;
	Dwarf_P_Cie cie;
	Dwarf_P_Fde fde;
	int ret;

	if (STAILQ_EMPTY(&dbg->dbgp_cielist))
		return (DW_DLE_NONE);

	/* Create .debug_frame section. */
	if ((ret = _dwarf_section_init(dbg, &ds, ".debug_frame", 0, error)) !=
	    DW_DLE_NONE)
		goto gen_fail0;

	/* Create relocation section for .debug_frame */
	RCHECK(_dwarf_reloc_section_init(dbg, &drs, ds, error));

	/* Generate list of CIE. */
	STAILQ_FOREACH(cie, &dbg->dbgp_cielist, cie_next)
		RCHECK(_dwarf_frame_gen_cie(dbg, ds, cie, error));

	/* Generate list of FDE. */
	STAILQ_FOREACH(fde, &dbg->dbgp_fdelist, fde_next)
		RCHECK(_dwarf_frame_gen_fde(dbg, ds, drs, fde, error));

	/* Inform application the creation of .debug_frame ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	/* Finalize relocation section for .debug_frame */
	RCHECK(_dwarf_reloc_section_finalize(dbg, drs, error));

	return (DW_DLE_NONE);

gen_fail:
	_dwarf_reloc_section_free(dbg, &drs);

gen_fail0:
	_dwarf_section_free(dbg, &ds);

	return (ret);
}

void
_dwarf_frame_pro_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_P_Cie cie, tcie;
	Dwarf_P_Fde fde, tfde;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	STAILQ_FOREACH_SAFE(cie, &dbg->dbgp_cielist, cie_next, tcie) {
		STAILQ_REMOVE(&dbg->dbgp_cielist, cie, _Dwarf_Cie, cie_next);
		if (cie->cie_augment)
			free(cie->cie_augment);
		if (cie->cie_initinst)
			free(cie->cie_initinst);
		free(cie);
	}
	dbg->dbgp_cielen = 0;

	STAILQ_FOREACH_SAFE(fde, &dbg->dbgp_fdelist, fde_next, tfde) {
		STAILQ_REMOVE(&dbg->dbgp_fdelist, fde, _Dwarf_Fde, fde_next);
		if (fde->fde_inst != NULL)
			free(fde->fde_inst);
		free(fde);
	}
	dbg->dbgp_fdelen = 0;
}
