/* $OpenBSD: aarch64_lse.c,v 1.1 2025/08/14 09:31:21 kettenis Exp $ */

/* Public domain */

/*
 * The CPU feature detection code from libcompiler-rt uses
 * elf_aux_info(3) which isn't available.  To prevent pulling in that
 * code, define a fake __aarch64_have_lse_atomics variable here.
 * Since LL/SC atomics are good enough for ramdisks, the default
 * initialization to false will work just fine on all machines.
 */
_Bool __aarch64_have_lse_atomics;
