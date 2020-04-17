/* SPDX-License-Identifier: GPL-2.0 */
/*
 * To prevent the compiler from emitting GOT-indirected (and thus absolute)
 * references to any global symbols, override their visibility as 'hidden'
 */
#pragma GCC visibility push(hidden)
