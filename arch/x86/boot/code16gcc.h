/* SPDX-License-Identifier: GPL-2.0 */
#
# code16gcc.h
#
# This file is added to the assembler via -Wa when compiling 16-bit C code.
# This is done this way instead via asm() to make sure gcc does not reorder
# things around us.
#
# gcc 4.9+ has a real -m16 option so we can drop this hack long term.
#

	.code16gcc
