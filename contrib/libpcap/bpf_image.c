/*
 * Copyright (c) 1990, 1991, 1992, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>

#include <stdio.h>
#include <string.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

char *
bpf_image(const struct bpf_insn *p, int n)
{
	const char *op;
	static char image[256];
	char operand_buf[64];
	const char *operand;

	switch (p->code) {

	default:
		op = "unimp";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "0x%x", p->code);
		operand = operand_buf;
		break;

	case BPF_RET|BPF_K:
		op = "ret";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_RET|BPF_A:
		op = "ret";
		operand = "";
		break;

	case BPF_LD|BPF_W|BPF_ABS:
		op = "ld";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_H|BPF_ABS:
		op = "ldh";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_B|BPF_ABS:
		op = "ldb";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_W|BPF_LEN:
		op = "ld";
		operand = "#pktlen";
		break;

	case BPF_LD|BPF_W|BPF_IND:
		op = "ld";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[x + %d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_H|BPF_IND:
		op = "ldh";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[x + %d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_B|BPF_IND:
		op = "ldb";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "[x + %d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_IMM:
		op = "ld";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_LDX|BPF_IMM:
		op = "ldx";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_LDX|BPF_MSH|BPF_B:
		op = "ldxb";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "4*([%d]&0xf)", p->k);
		operand = operand_buf;
		break;

	case BPF_LD|BPF_MEM:
		op = "ld";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "M[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_LDX|BPF_MEM:
		op = "ldx";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "M[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_ST:
		op = "st";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "M[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_STX:
		op = "stx";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "M[%d]", p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JA:
		op = "ja";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "%d", n + 1 + p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JGT|BPF_K:
		op = "jgt";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JGE|BPF_K:
		op = "jge";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JEQ|BPF_K:
		op = "jeq";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JSET|BPF_K:
		op = "jset";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_JMP|BPF_JGT|BPF_X:
		op = "jgt";
		operand = "x";
		break;

	case BPF_JMP|BPF_JGE|BPF_X:
		op = "jge";
		operand = "x";
		break;

	case BPF_JMP|BPF_JEQ|BPF_X:
		op = "jeq";
		operand = "x";
		break;

	case BPF_JMP|BPF_JSET|BPF_X:
		op = "jset";
		operand = "x";
		break;

	case BPF_ALU|BPF_ADD|BPF_X:
		op = "add";
		operand = "x";
		break;

	case BPF_ALU|BPF_SUB|BPF_X:
		op = "sub";
		operand = "x";
		break;

	case BPF_ALU|BPF_MUL|BPF_X:
		op = "mul";
		operand = "x";
		break;

	case BPF_ALU|BPF_DIV|BPF_X:
		op = "div";
		operand = "x";
		break;

	case BPF_ALU|BPF_MOD|BPF_X:
		op = "mod";
		operand = "x";
		break;

	case BPF_ALU|BPF_AND|BPF_X:
		op = "and";
		operand = "x";
		break;

	case BPF_ALU|BPF_OR|BPF_X:
		op = "or";
		operand = "x";
		break;

	case BPF_ALU|BPF_XOR|BPF_X:
		op = "xor";
		operand = "x";
		break;

	case BPF_ALU|BPF_LSH|BPF_X:
		op = "lsh";
		operand = "x";
		break;

	case BPF_ALU|BPF_RSH|BPF_X:
		op = "rsh";
		operand = "x";
		break;

	case BPF_ALU|BPF_ADD|BPF_K:
		op = "add";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_SUB|BPF_K:
		op = "sub";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_MUL|BPF_K:
		op = "mul";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_DIV|BPF_K:
		op = "div";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_MOD|BPF_K:
		op = "mod";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_AND|BPF_K:
		op = "and";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_OR|BPF_K:
		op = "or";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_XOR|BPF_K:
		op = "xor";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#0x%x", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_LSH|BPF_K:
		op = "lsh";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_RSH|BPF_K:
		op = "rsh";
		(void)pcap_snprintf(operand_buf, sizeof operand_buf, "#%d", p->k);
		operand = operand_buf;
		break;

	case BPF_ALU|BPF_NEG:
		op = "neg";
		operand = "";
		break;

	case BPF_MISC|BPF_TAX:
		op = "tax";
		operand = "";
		break;

	case BPF_MISC|BPF_TXA:
		op = "txa";
		operand = "";
		break;
	}
	if (BPF_CLASS(p->code) == BPF_JMP && BPF_OP(p->code) != BPF_JA) {
		(void)pcap_snprintf(image, sizeof image,
			      "(%03d) %-8s %-16s jt %d\tjf %d",
			      n, op, operand, n + 1 + p->jt, n + 1 + p->jf);
	} else {
		(void)pcap_snprintf(image, sizeof image,
			      "(%03d) %-8s %s",
			      n, op, operand);
	}
	return image;
}
