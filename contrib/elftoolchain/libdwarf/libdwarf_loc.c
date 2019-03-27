/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2014 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_loc.c 3070 2014-06-23 03:08:33Z kaiwang27 $");

/*
 * Given an array of bytes of length 'len' representing a
 * DWARF expression, compute the number of operations based
 * on there being one byte describing the operation and
 * zero or more bytes of operands as defined in the standard
 * for each operation type. Also, if lbuf is non-null, store
 * the opcode and oprand in it.
 */
static int
_dwarf_loc_fill_loc(Dwarf_Debug dbg, Dwarf_Locdesc *lbuf, uint8_t pointer_size,
    uint8_t offset_size, uint8_t version, uint8_t *p, int len)
{
	int count;
	uint64_t operand1;
	uint64_t operand2;
	uint8_t *ps, *pe, s;

	count = 0;
	ps = p;
	pe = p + len;

	/*
	 * Process each byte. If an error occurs, then the
	 * count will be set to -1.
	 */
	while (p < pe) {

		operand1 = 0;
		operand2 = 0;

		if (lbuf != NULL) {
			lbuf->ld_s[count].lr_atom = *p;
			lbuf->ld_s[count].lr_offset = p - ps;
		}

		switch (*p++) {
		/* Operations with no operands. */
		case DW_OP_deref:
		case DW_OP_reg0:
		case DW_OP_reg1:
		case DW_OP_reg2:
		case DW_OP_reg3:
		case DW_OP_reg4:
		case DW_OP_reg5:
		case DW_OP_reg6:
		case DW_OP_reg7:
		case DW_OP_reg8:
		case DW_OP_reg9:
		case DW_OP_reg10:
		case DW_OP_reg11:
		case DW_OP_reg12:
		case DW_OP_reg13:
		case DW_OP_reg14:
		case DW_OP_reg15:
		case DW_OP_reg16:
		case DW_OP_reg17:
		case DW_OP_reg18:
		case DW_OP_reg19:
		case DW_OP_reg20:
		case DW_OP_reg21:
		case DW_OP_reg22:
		case DW_OP_reg23:
		case DW_OP_reg24:
		case DW_OP_reg25:
		case DW_OP_reg26:
		case DW_OP_reg27:
		case DW_OP_reg28:
		case DW_OP_reg29:
		case DW_OP_reg30:
		case DW_OP_reg31:

		case DW_OP_lit0:
		case DW_OP_lit1:
		case DW_OP_lit2:
		case DW_OP_lit3:
		case DW_OP_lit4:
		case DW_OP_lit5:
		case DW_OP_lit6:
		case DW_OP_lit7:
		case DW_OP_lit8:
		case DW_OP_lit9:
		case DW_OP_lit10:
		case DW_OP_lit11:
		case DW_OP_lit12:
		case DW_OP_lit13:
		case DW_OP_lit14:
		case DW_OP_lit15:
		case DW_OP_lit16:
		case DW_OP_lit17:
		case DW_OP_lit18:
		case DW_OP_lit19:
		case DW_OP_lit20:
		case DW_OP_lit21:
		case DW_OP_lit22:
		case DW_OP_lit23:
		case DW_OP_lit24:
		case DW_OP_lit25:
		case DW_OP_lit26:
		case DW_OP_lit27:
		case DW_OP_lit28:
		case DW_OP_lit29:
		case DW_OP_lit30:
		case DW_OP_lit31:

		case DW_OP_dup:
		case DW_OP_drop:

		case DW_OP_over:

		case DW_OP_swap:
		case DW_OP_rot:
		case DW_OP_xderef:

		case DW_OP_abs:
		case DW_OP_and:
		case DW_OP_div:
		case DW_OP_minus:
		case DW_OP_mod:
		case DW_OP_mul:
		case DW_OP_neg:
		case DW_OP_not:
		case DW_OP_or:
		case DW_OP_plus:

		case DW_OP_shl:
		case DW_OP_shr:
		case DW_OP_shra:
		case DW_OP_xor:

		case DW_OP_eq:
		case DW_OP_ge:
		case DW_OP_gt:
		case DW_OP_le:
		case DW_OP_lt:
		case DW_OP_ne:

		case DW_OP_nop:
		case DW_OP_push_object_address:
		case DW_OP_form_tls_address:
		case DW_OP_call_frame_cfa:
		case DW_OP_stack_value:
		case DW_OP_GNU_push_tls_address:
		case DW_OP_GNU_uninit:
			break;

		/* Operations with 1-byte operands. */
		case DW_OP_const1u:
		case DW_OP_pick:
		case DW_OP_deref_size:
		case DW_OP_xderef_size:
			operand1 = *p++;
			break;

		case DW_OP_const1s:
			operand1 = (int8_t) *p++;
			break;

		/* Operations with 2-byte operands. */
		case DW_OP_call2:
		case DW_OP_const2u:
		case DW_OP_bra:
		case DW_OP_skip:
			operand1 = dbg->decode(&p, 2);
			break;

		case DW_OP_const2s:
			operand1 = (int16_t) dbg->decode(&p, 2);
			break;

		/* Operations with 4-byte operands. */
		case DW_OP_call4:
		case DW_OP_const4u:
		case DW_OP_GNU_parameter_ref:
			operand1 = dbg->decode(&p, 4);
			break;

		case DW_OP_const4s:
			operand1 = (int32_t) dbg->decode(&p, 4);
			break;

		/* Operations with 8-byte operands. */
		case DW_OP_const8u:
		case DW_OP_const8s:
			operand1 = dbg->decode(&p, 8);
			break;

		/* Operations with an unsigned LEB128 operand. */
		case DW_OP_constu:
		case DW_OP_plus_uconst:
		case DW_OP_regx:
		case DW_OP_piece:
		case DW_OP_GNU_deref_type:
		case DW_OP_GNU_convert:
		case DW_OP_GNU_reinterpret:
			operand1 = _dwarf_decode_uleb128(&p);
			break;

		/* Operations with a signed LEB128 operand. */
		case DW_OP_consts:
		case DW_OP_breg0:
		case DW_OP_breg1:
		case DW_OP_breg2:
		case DW_OP_breg3:
		case DW_OP_breg4:
		case DW_OP_breg5:
		case DW_OP_breg6:
		case DW_OP_breg7:
		case DW_OP_breg8:
		case DW_OP_breg9:
		case DW_OP_breg10:
		case DW_OP_breg11:
		case DW_OP_breg12:
		case DW_OP_breg13:
		case DW_OP_breg14:
		case DW_OP_breg15:
		case DW_OP_breg16:
		case DW_OP_breg17:
		case DW_OP_breg18:
		case DW_OP_breg19:
		case DW_OP_breg20:
		case DW_OP_breg21:
		case DW_OP_breg22:
		case DW_OP_breg23:
		case DW_OP_breg24:
		case DW_OP_breg25:
		case DW_OP_breg26:
		case DW_OP_breg27:
		case DW_OP_breg28:
		case DW_OP_breg29:
		case DW_OP_breg30:
		case DW_OP_breg31:
		case DW_OP_fbreg:
			operand1 = _dwarf_decode_sleb128(&p);
			break;

		/*
		 * Oeration with two unsigned LEB128 operands.
		 */
		case DW_OP_bit_piece:
		case DW_OP_GNU_regval_type:
			operand1 = _dwarf_decode_uleb128(&p);
			operand2 = _dwarf_decode_uleb128(&p);
			break;

		/*
		 * Operations with an unsigned LEB128 operand
		 * followed by a signed LEB128 operand.
		 */
		case DW_OP_bregx:
			operand1 = _dwarf_decode_uleb128(&p);
			operand2 = _dwarf_decode_sleb128(&p);
			break;

		/*
		 * Operation with an unsigned LEB128 operand
		 * representing the size of a block, followed
		 * by the block content.
		 *
		 * Store the size of the block in the operand1
		 * and a pointer to the block in the operand2.
		 */
		case DW_OP_implicit_value:
		case DW_OP_GNU_entry_value:
			operand1 = _dwarf_decode_uleb128(&p);
			operand2 = (Dwarf_Unsigned) (uintptr_t) p;
			p += operand1;
			break;

		/* Target address size operand. */
		case DW_OP_addr:
		case DW_OP_GNU_addr_index:
		case DW_OP_GNU_const_index:
			operand1 = dbg->decode(&p, pointer_size);
			break;

		/* Offset size operand. */
		case DW_OP_call_ref:
			operand1 = dbg->decode(&p, offset_size);
			break;

		/*
		 * The first byte is address byte length, followed by
		 * the address value. If the length is 0, the address
		 * size is the same as target pointer size.
		 */
		case DW_OP_GNU_encoded_addr:
			s = *p++;
			if (s == 0)
				s = pointer_size;
			operand1 = dbg->decode(&p, s);
			break;

		/*
		 * Operand1: DIE offset (size depending on DWARF version)
		 * DWARF2: pointer size
		 * DWARF{3,4}: offset size
		 *
		 * Operand2: SLEB128
		 */
		case DW_OP_GNU_implicit_pointer:
			if (version == 2)
				operand1 = dbg->decode(&p, pointer_size);
			else
				operand1 = dbg->decode(&p, offset_size);
			operand2 = _dwarf_decode_sleb128(&p);
			break;

		/*
		 * Operand1: DIE offset (ULEB128)
		 * Operand2: pointer to a block. The block's first byte
		 * is its size.
		 */
		case DW_OP_GNU_const_type:
			operand1 = _dwarf_decode_uleb128(&p);
			operand2 = (Dwarf_Unsigned) (uintptr_t) p;
			s = *p++;
			p += s;
			break;

		/* All other operations cause an error. */
		default:
			count = -1;
			goto done;
		}

		if (lbuf != NULL) {
			lbuf->ld_s[count].lr_number = operand1;
			lbuf->ld_s[count].lr_number2 = operand2;
		}

		count++;
	}

done:
	return (count);
}

int
_dwarf_loc_expr_add_atom(Dwarf_Debug dbg, uint8_t *out, uint8_t *end,
    Dwarf_Small atom, Dwarf_Unsigned operand1, Dwarf_Unsigned operand2,
    int *length, Dwarf_Error *error)
{
	uint8_t buf[64];
	uint8_t *p, *pe;
	uint64_t offset;
	int len;

	if (out != NULL && end != NULL) {
		p = out;
		pe = end;
	} else {
		p = out = buf;
		pe = &buf[sizeof(buf)];
	}

	switch (atom) {
	/* Operations with no operands. */
	case DW_OP_deref:
	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:

	case DW_OP_lit0:
	case DW_OP_lit1:
	case DW_OP_lit2:
	case DW_OP_lit3:
	case DW_OP_lit4:
	case DW_OP_lit5:
	case DW_OP_lit6:
	case DW_OP_lit7:
	case DW_OP_lit8:
	case DW_OP_lit9:
	case DW_OP_lit10:
	case DW_OP_lit11:
	case DW_OP_lit12:
	case DW_OP_lit13:
	case DW_OP_lit14:
	case DW_OP_lit15:
	case DW_OP_lit16:
	case DW_OP_lit17:
	case DW_OP_lit18:
	case DW_OP_lit19:
	case DW_OP_lit20:
	case DW_OP_lit21:
	case DW_OP_lit22:
	case DW_OP_lit23:
	case DW_OP_lit24:
	case DW_OP_lit25:
	case DW_OP_lit26:
	case DW_OP_lit27:
	case DW_OP_lit28:
	case DW_OP_lit29:
	case DW_OP_lit30:
	case DW_OP_lit31:

	case DW_OP_dup:
	case DW_OP_drop:

	case DW_OP_over:

	case DW_OP_swap:
	case DW_OP_rot:
	case DW_OP_xderef:

	case DW_OP_abs:
	case DW_OP_and:
	case DW_OP_div:
	case DW_OP_minus:
	case DW_OP_mod:
	case DW_OP_mul:
	case DW_OP_neg:
	case DW_OP_not:
	case DW_OP_or:
	case DW_OP_plus:

	case DW_OP_shl:
	case DW_OP_shr:
	case DW_OP_shra:
	case DW_OP_xor:

	case DW_OP_eq:
	case DW_OP_ge:
	case DW_OP_gt:
	case DW_OP_le:
	case DW_OP_lt:
	case DW_OP_ne:

	case DW_OP_nop:
	case DW_OP_GNU_push_tls_address:
		*p++ = atom;
		break;

	/* Operations with 1-byte operands. */
	case DW_OP_const1u:
	case DW_OP_const1s:
	case DW_OP_pick:
	case DW_OP_deref_size:
	case DW_OP_xderef_size:
		*p++ = atom;
		*p++ = (uint8_t) operand1;
		break;

	/* Operations with 2-byte operands. */
	case DW_OP_const2u:
	case DW_OP_const2s:
	case DW_OP_bra:
	case DW_OP_skip:
		*p++ = atom;
		offset = 0;
		dbg->write(p, &offset, operand1, 2);
		p += 2;
		break;

	/* Operations with 4-byte operands. */
	case DW_OP_const4u:
	case DW_OP_const4s:
		*p++ = atom;
		offset = 0;
		dbg->write(p, &offset, operand1, 4);
		p += 4;
		break;

	/* Operations with 8-byte operands. */
	case DW_OP_const8u:
	case DW_OP_const8s:
		*p++ = atom;
		offset = 0;
		dbg->write(p, &offset, operand1, 8);
		p += 8;
		break;

	/* Operations with an unsigned LEB128 operand. */
	case DW_OP_constu:
	case DW_OP_plus_uconst:
	case DW_OP_regx:
	case DW_OP_piece:
		*p++ = atom;
		len = _dwarf_write_uleb128(p, pe, operand1);
		assert(len > 0);
		p += len;
		break;

	/* Operations with a signed LEB128 operand. */
	case DW_OP_consts:
	case DW_OP_breg0:
	case DW_OP_breg1:
	case DW_OP_breg2:
	case DW_OP_breg3:
	case DW_OP_breg4:
	case DW_OP_breg5:
	case DW_OP_breg6:
	case DW_OP_breg7:
	case DW_OP_breg8:
	case DW_OP_breg9:
	case DW_OP_breg10:
	case DW_OP_breg11:
	case DW_OP_breg12:
	case DW_OP_breg13:
	case DW_OP_breg14:
	case DW_OP_breg15:
	case DW_OP_breg16:
	case DW_OP_breg17:
	case DW_OP_breg18:
	case DW_OP_breg19:
	case DW_OP_breg20:
	case DW_OP_breg21:
	case DW_OP_breg22:
	case DW_OP_breg23:
	case DW_OP_breg24:
	case DW_OP_breg25:
	case DW_OP_breg26:
	case DW_OP_breg27:
	case DW_OP_breg28:
	case DW_OP_breg29:
	case DW_OP_breg30:
	case DW_OP_breg31:
	case DW_OP_fbreg:
		*p++ = atom;
		len = _dwarf_write_sleb128(p, pe, operand1);
		assert(len > 0);
		p += len;
		break;

	/*
	 * Operations with an unsigned LEB128 operand
	 * followed by a signed LEB128 operand.
	 */
	case DW_OP_bregx:
		*p++ = atom;
		len = _dwarf_write_uleb128(p, pe, operand1);
		assert(len > 0);
		p += len;
		len = _dwarf_write_sleb128(p, pe, operand2);
		assert(len > 0);
		p += len;
		break;

	/* Target address size operand. */
	case DW_OP_addr:
		*p++ = atom;
		offset = 0;
		dbg->write(p, &offset, operand1, dbg->dbg_pointer_size);
		p += dbg->dbg_pointer_size;
		break;

	/* All other operations cause an error. */
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_LOC_EXPR_BAD);
		return (DW_DLE_LOC_EXPR_BAD);
	}

	if (length)
		*length = p - out;

	return (DW_DLE_NONE);
}

int
_dwarf_loc_fill_locdesc(Dwarf_Debug dbg, Dwarf_Locdesc *llbuf, uint8_t *in,
    uint64_t in_len, uint8_t pointer_size, uint8_t offset_size,
    uint8_t version, Dwarf_Error *error)
{
	int num;

	assert(llbuf != NULL);
	assert(in != NULL);
	assert(in_len > 0);

	/* Compute the number of locations. */
	if ((num = _dwarf_loc_fill_loc(dbg, NULL, pointer_size, offset_size,
	    version, in, in_len)) < 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_LOC_EXPR_BAD);
		return (DW_DLE_LOC_EXPR_BAD);
	}

	llbuf->ld_cents = num;
	if (num <= 0)
		return (DW_DLE_NONE);

	if ((llbuf->ld_s = calloc(num, sizeof(Dwarf_Loc))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	(void) _dwarf_loc_fill_loc(dbg, llbuf, pointer_size, offset_size,
	    version, in, in_len);

	return (DW_DLE_NONE);
}

int
_dwarf_loc_fill_locexpr(Dwarf_Debug dbg, Dwarf_Locdesc **ret_llbuf, uint8_t *in,
    uint64_t in_len, uint8_t pointer_size, uint8_t offset_size,
    uint8_t version, Dwarf_Error *error)
{
	Dwarf_Locdesc *llbuf;
	int ret;

	if ((llbuf = malloc(sizeof(Dwarf_Locdesc))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	llbuf->ld_lopc = 0;
	llbuf->ld_hipc = ~0ULL;
	llbuf->ld_s = NULL;

	ret = _dwarf_loc_fill_locdesc(dbg, llbuf, in, in_len, pointer_size,
	    offset_size, version, error);
	if (ret != DW_DLE_NONE) {
		free(llbuf);
		return (ret);
	}

	*ret_llbuf = llbuf;

	return (ret);
}

int
_dwarf_loc_add(Dwarf_Die die, Dwarf_Attribute at, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_CU cu;
	int ret;

	assert(at->at_ld == NULL);
	assert(at->u[1].u8p != NULL);
	assert(at->u[0].u64 > 0);

	cu = die->die_cu;
	assert(cu != NULL);

	dbg = cu->cu_dbg;
	assert(dbg != NULL);

	ret = _dwarf_loc_fill_locexpr(dbg, &at->at_ld, at->u[1].u8p,
	    at->u[0].u64, cu->cu_pointer_size, cu->cu_length_size == 4 ? 4 : 8,
	    cu->cu_version, error);

	return (ret);
}
