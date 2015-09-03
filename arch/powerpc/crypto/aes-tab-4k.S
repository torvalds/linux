/*
 * 4K AES tables for PPC AES implementation
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

/*
 * These big endian AES encryption/decryption tables have been taken from
 * crypto/aes_generic.c and are designed to be simply accessed by a combination
 * of rlwimi/lwz instructions with a minimum of table registers (usually only
 * one required). Thus they are aligned to 4K. The locality of rotated values
 * is derived from the reduced offsets that are available in the SPE load
 * instructions. E.g. evldw, evlwwsplat, ...
 *
 * For the safety-conscious it has to be noted that they might be vulnerable
 * to cache timing attacks because of their size. Nevertheless in contrast to
 * the generic tables they have been reduced from 16KB to 8KB + 256 bytes.
 * This is a quite good tradeoff for low power devices (e.g. routers) without
 * dedicated encryption hardware where we usually have no multiuser
 * environment.
 *
 */

#define R(a, b, c, d) \
	0x##a##b##c##d, 0x##d##a##b##c, 0x##c##d##a##b, 0x##b##c##d##a

.data
.align 12
.globl PPC_AES_4K_ENCTAB
PPC_AES_4K_ENCTAB:
/* encryption table, same as crypto_ft_tab in crypto/aes-generic.c */
	.long R(c6, 63, 63, a5), R(f8, 7c, 7c, 84)
	.long R(ee, 77, 77, 99), R(f6, 7b, 7b, 8d)
	.long R(ff, f2, f2, 0d), R(d6, 6b, 6b, bd)
	.long R(de, 6f, 6f, b1), R(91, c5, c5, 54)
	.long R(60, 30, 30, 50), R(02, 01, 01, 03)
	.long R(ce, 67, 67, a9), R(56, 2b, 2b, 7d)
	.long R(e7, fe, fe, 19), R(b5, d7, d7, 62)
	.long R(4d, ab, ab, e6), R(ec, 76, 76, 9a)
	.long R(8f, ca, ca, 45), R(1f, 82, 82, 9d)
	.long R(89, c9, c9, 40), R(fa, 7d, 7d, 87)
	.long R(ef, fa, fa, 15), R(b2, 59, 59, eb)
	.long R(8e, 47, 47, c9), R(fb, f0, f0, 0b)
	.long R(41, ad, ad, ec), R(b3, d4, d4, 67)
	.long R(5f, a2, a2, fd), R(45, af, af, ea)
	.long R(23, 9c, 9c, bf), R(53, a4, a4, f7)
	.long R(e4, 72, 72, 96), R(9b, c0, c0, 5b)
	.long R(75, b7, b7, c2), R(e1, fd, fd, 1c)
	.long R(3d, 93, 93, ae), R(4c, 26, 26, 6a)
	.long R(6c, 36, 36, 5a), R(7e, 3f, 3f, 41)
	.long R(f5, f7, f7, 02), R(83, cc, cc, 4f)
	.long R(68, 34, 34, 5c), R(51, a5, a5, f4)
	.long R(d1, e5, e5, 34), R(f9, f1, f1, 08)
	.long R(e2, 71, 71, 93), R(ab, d8, d8, 73)
	.long R(62, 31, 31, 53), R(2a, 15, 15, 3f)
	.long R(08, 04, 04, 0c), R(95, c7, c7, 52)
	.long R(46, 23, 23, 65), R(9d, c3, c3, 5e)
	.long R(30, 18, 18, 28), R(37, 96, 96, a1)
	.long R(0a, 05, 05, 0f), R(2f, 9a, 9a, b5)
	.long R(0e, 07, 07, 09), R(24, 12, 12, 36)
	.long R(1b, 80, 80, 9b), R(df, e2, e2, 3d)
	.long R(cd, eb, eb, 26), R(4e, 27, 27, 69)
	.long R(7f, b2, b2, cd), R(ea, 75, 75, 9f)
	.long R(12, 09, 09, 1b), R(1d, 83, 83, 9e)
	.long R(58, 2c, 2c, 74), R(34, 1a, 1a, 2e)
	.long R(36, 1b, 1b, 2d), R(dc, 6e, 6e, b2)
	.long R(b4, 5a, 5a, ee), R(5b, a0, a0, fb)
	.long R(a4, 52, 52, f6), R(76, 3b, 3b, 4d)
	.long R(b7, d6, d6, 61), R(7d, b3, b3, ce)
	.long R(52, 29, 29, 7b), R(dd, e3, e3, 3e)
	.long R(5e, 2f, 2f, 71), R(13, 84, 84, 97)
	.long R(a6, 53, 53, f5), R(b9, d1, d1, 68)
	.long R(00, 00, 00, 00), R(c1, ed, ed, 2c)
	.long R(40, 20, 20, 60), R(e3, fc, fc, 1f)
	.long R(79, b1, b1, c8), R(b6, 5b, 5b, ed)
	.long R(d4, 6a, 6a, be), R(8d, cb, cb, 46)
	.long R(67, be, be, d9), R(72, 39, 39, 4b)
	.long R(94, 4a, 4a, de), R(98, 4c, 4c, d4)
	.long R(b0, 58, 58, e8), R(85, cf, cf, 4a)
	.long R(bb, d0, d0, 6b), R(c5, ef, ef, 2a)
	.long R(4f, aa, aa, e5), R(ed, fb, fb, 16)
	.long R(86, 43, 43, c5), R(9a, 4d, 4d, d7)
	.long R(66, 33, 33, 55), R(11, 85, 85, 94)
	.long R(8a, 45, 45, cf), R(e9, f9, f9, 10)
	.long R(04, 02, 02, 06), R(fe, 7f, 7f, 81)
	.long R(a0, 50, 50, f0), R(78, 3c, 3c, 44)
	.long R(25, 9f, 9f, ba), R(4b, a8, a8, e3)
	.long R(a2, 51, 51, f3), R(5d, a3, a3, fe)
	.long R(80, 40, 40, c0), R(05, 8f, 8f, 8a)
	.long R(3f, 92, 92, ad), R(21, 9d, 9d, bc)
	.long R(70, 38, 38, 48), R(f1, f5, f5, 04)
	.long R(63, bc, bc, df), R(77, b6, b6, c1)
	.long R(af, da, da, 75), R(42, 21, 21, 63)
	.long R(20, 10, 10, 30), R(e5, ff, ff, 1a)
	.long R(fd, f3, f3, 0e), R(bf, d2, d2, 6d)
	.long R(81, cd, cd, 4c), R(18, 0c, 0c, 14)
	.long R(26, 13, 13, 35), R(c3, ec, ec, 2f)
	.long R(be, 5f, 5f, e1), R(35, 97, 97, a2)
	.long R(88, 44, 44, cc), R(2e, 17, 17, 39)
	.long R(93, c4, c4, 57), R(55, a7, a7, f2)
	.long R(fc, 7e, 7e, 82), R(7a, 3d, 3d, 47)
	.long R(c8, 64, 64, ac), R(ba, 5d, 5d, e7)
	.long R(32, 19, 19, 2b), R(e6, 73, 73, 95)
	.long R(c0, 60, 60, a0), R(19, 81, 81, 98)
	.long R(9e, 4f, 4f, d1), R(a3, dc, dc, 7f)
	.long R(44, 22, 22, 66), R(54, 2a, 2a, 7e)
	.long R(3b, 90, 90, ab), R(0b, 88, 88, 83)
	.long R(8c, 46, 46, ca), R(c7, ee, ee, 29)
	.long R(6b, b8, b8, d3), R(28, 14, 14, 3c)
	.long R(a7, de, de, 79), R(bc, 5e, 5e, e2)
	.long R(16, 0b, 0b, 1d), R(ad, db, db, 76)
	.long R(db, e0, e0, 3b), R(64, 32, 32, 56)
	.long R(74, 3a, 3a, 4e), R(14, 0a, 0a, 1e)
	.long R(92, 49, 49, db), R(0c, 06, 06, 0a)
	.long R(48, 24, 24, 6c), R(b8, 5c, 5c, e4)
	.long R(9f, c2, c2, 5d), R(bd, d3, d3, 6e)
	.long R(43, ac, ac, ef), R(c4, 62, 62, a6)
	.long R(39, 91, 91, a8), R(31, 95, 95, a4)
	.long R(d3, e4, e4, 37), R(f2, 79, 79, 8b)
	.long R(d5, e7, e7, 32), R(8b, c8, c8, 43)
	.long R(6e, 37, 37, 59), R(da, 6d, 6d, b7)
	.long R(01, 8d, 8d, 8c), R(b1, d5, d5, 64)
	.long R(9c, 4e, 4e, d2), R(49, a9, a9, e0)
	.long R(d8, 6c, 6c, b4), R(ac, 56, 56, fa)
	.long R(f3, f4, f4, 07), R(cf, ea, ea, 25)
	.long R(ca, 65, 65, af), R(f4, 7a, 7a, 8e)
	.long R(47, ae, ae, e9), R(10, 08, 08, 18)
	.long R(6f, ba, ba, d5), R(f0, 78, 78, 88)
	.long R(4a, 25, 25, 6f), R(5c, 2e, 2e, 72)
	.long R(38, 1c, 1c, 24), R(57, a6, a6, f1)
	.long R(73, b4, b4, c7), R(97, c6, c6, 51)
	.long R(cb, e8, e8, 23), R(a1, dd, dd, 7c)
	.long R(e8, 74, 74, 9c), R(3e, 1f, 1f, 21)
	.long R(96, 4b, 4b, dd), R(61, bd, bd, dc)
	.long R(0d, 8b, 8b, 86), R(0f, 8a, 8a, 85)
	.long R(e0, 70, 70, 90), R(7c, 3e, 3e, 42)
	.long R(71, b5, b5, c4), R(cc, 66, 66, aa)
	.long R(90, 48, 48, d8), R(06, 03, 03, 05)
	.long R(f7, f6, f6, 01), R(1c, 0e, 0e, 12)
	.long R(c2, 61, 61, a3), R(6a, 35, 35, 5f)
	.long R(ae, 57, 57, f9), R(69, b9, b9, d0)
	.long R(17, 86, 86, 91), R(99, c1, c1, 58)
	.long R(3a, 1d, 1d, 27), R(27, 9e, 9e, b9)
	.long R(d9, e1, e1, 38), R(eb, f8, f8, 13)
	.long R(2b, 98, 98, b3), R(22, 11, 11, 33)
	.long R(d2, 69, 69, bb), R(a9, d9, d9, 70)
	.long R(07, 8e, 8e, 89), R(33, 94, 94, a7)
	.long R(2d, 9b, 9b, b6), R(3c, 1e, 1e, 22)
	.long R(15, 87, 87, 92), R(c9, e9, e9, 20)
	.long R(87, ce, ce, 49), R(aa, 55, 55, ff)
	.long R(50, 28, 28, 78), R(a5, df, df, 7a)
	.long R(03, 8c, 8c, 8f), R(59, a1, a1, f8)
	.long R(09, 89, 89, 80), R(1a, 0d, 0d, 17)
	.long R(65, bf, bf, da), R(d7, e6, e6, 31)
	.long R(84, 42, 42, c6), R(d0, 68, 68, b8)
	.long R(82, 41, 41, c3), R(29, 99, 99, b0)
	.long R(5a, 2d, 2d, 77), R(1e, 0f, 0f, 11)
	.long R(7b, b0, b0, cb), R(a8, 54, 54, fc)
	.long R(6d, bb, bb, d6), R(2c, 16, 16, 3a)
.globl PPC_AES_4K_DECTAB
PPC_AES_4K_DECTAB:
/* decryption table, same as crypto_it_tab in crypto/aes-generic.c */
	.long R(51, f4, a7, 50), R(7e, 41, 65, 53)
	.long R(1a, 17, a4, c3), R(3a, 27, 5e, 96)
	.long R(3b, ab, 6b, cb), R(1f, 9d, 45, f1)
	.long R(ac, fa, 58, ab), R(4b, e3, 03, 93)
	.long R(20, 30, fa, 55), R(ad, 76, 6d, f6)
	.long R(88, cc, 76, 91), R(f5, 02, 4c, 25)
	.long R(4f, e5, d7, fc), R(c5, 2a, cb, d7)
	.long R(26, 35, 44, 80), R(b5, 62, a3, 8f)
	.long R(de, b1, 5a, 49), R(25, ba, 1b, 67)
	.long R(45, ea, 0e, 98), R(5d, fe, c0, e1)
	.long R(c3, 2f, 75, 02), R(81, 4c, f0, 12)
	.long R(8d, 46, 97, a3), R(6b, d3, f9, c6)
	.long R(03, 8f, 5f, e7), R(15, 92, 9c, 95)
	.long R(bf, 6d, 7a, eb), R(95, 52, 59, da)
	.long R(d4, be, 83, 2d), R(58, 74, 21, d3)
	.long R(49, e0, 69, 29), R(8e, c9, c8, 44)
	.long R(75, c2, 89, 6a), R(f4, 8e, 79, 78)
	.long R(99, 58, 3e, 6b), R(27, b9, 71, dd)
	.long R(be, e1, 4f, b6), R(f0, 88, ad, 17)
	.long R(c9, 20, ac, 66), R(7d, ce, 3a, b4)
	.long R(63, df, 4a, 18), R(e5, 1a, 31, 82)
	.long R(97, 51, 33, 60), R(62, 53, 7f, 45)
	.long R(b1, 64, 77, e0), R(bb, 6b, ae, 84)
	.long R(fe, 81, a0, 1c), R(f9, 08, 2b, 94)
	.long R(70, 48, 68, 58), R(8f, 45, fd, 19)
	.long R(94, de, 6c, 87), R(52, 7b, f8, b7)
	.long R(ab, 73, d3, 23), R(72, 4b, 02, e2)
	.long R(e3, 1f, 8f, 57), R(66, 55, ab, 2a)
	.long R(b2, eb, 28, 07), R(2f, b5, c2, 03)
	.long R(86, c5, 7b, 9a), R(d3, 37, 08, a5)
	.long R(30, 28, 87, f2), R(23, bf, a5, b2)
	.long R(02, 03, 6a, ba), R(ed, 16, 82, 5c)
	.long R(8a, cf, 1c, 2b), R(a7, 79, b4, 92)
	.long R(f3, 07, f2, f0), R(4e, 69, e2, a1)
	.long R(65, da, f4, cd), R(06, 05, be, d5)
	.long R(d1, 34, 62, 1f), R(c4, a6, fe, 8a)
	.long R(34, 2e, 53, 9d), R(a2, f3, 55, a0)
	.long R(05, 8a, e1, 32), R(a4, f6, eb, 75)
	.long R(0b, 83, ec, 39), R(40, 60, ef, aa)
	.long R(5e, 71, 9f, 06), R(bd, 6e, 10, 51)
	.long R(3e, 21, 8a, f9), R(96, dd, 06, 3d)
	.long R(dd, 3e, 05, ae), R(4d, e6, bd, 46)
	.long R(91, 54, 8d, b5), R(71, c4, 5d, 05)
	.long R(04, 06, d4, 6f), R(60, 50, 15, ff)
	.long R(19, 98, fb, 24), R(d6, bd, e9, 97)
	.long R(89, 40, 43, cc), R(67, d9, 9e, 77)
	.long R(b0, e8, 42, bd), R(07, 89, 8b, 88)
	.long R(e7, 19, 5b, 38), R(79, c8, ee, db)
	.long R(a1, 7c, 0a, 47), R(7c, 42, 0f, e9)
	.long R(f8, 84, 1e, c9), R(00, 00, 00, 00)
	.long R(09, 80, 86, 83), R(32, 2b, ed, 48)
	.long R(1e, 11, 70, ac), R(6c, 5a, 72, 4e)
	.long R(fd, 0e, ff, fb), R(0f, 85, 38, 56)
	.long R(3d, ae, d5, 1e), R(36, 2d, 39, 27)
	.long R(0a, 0f, d9, 64), R(68, 5c, a6, 21)
	.long R(9b, 5b, 54, d1), R(24, 36, 2e, 3a)
	.long R(0c, 0a, 67, b1), R(93, 57, e7, 0f)
	.long R(b4, ee, 96, d2), R(1b, 9b, 91, 9e)
	.long R(80, c0, c5, 4f), R(61, dc, 20, a2)
	.long R(5a, 77, 4b, 69), R(1c, 12, 1a, 16)
	.long R(e2, 93, ba, 0a), R(c0, a0, 2a, e5)
	.long R(3c, 22, e0, 43), R(12, 1b, 17, 1d)
	.long R(0e, 09, 0d, 0b), R(f2, 8b, c7, ad)
	.long R(2d, b6, a8, b9), R(14, 1e, a9, c8)
	.long R(57, f1, 19, 85), R(af, 75, 07, 4c)
	.long R(ee, 99, dd, bb), R(a3, 7f, 60, fd)
	.long R(f7, 01, 26, 9f), R(5c, 72, f5, bc)
	.long R(44, 66, 3b, c5), R(5b, fb, 7e, 34)
	.long R(8b, 43, 29, 76), R(cb, 23, c6, dc)
	.long R(b6, ed, fc, 68), R(b8, e4, f1, 63)
	.long R(d7, 31, dc, ca), R(42, 63, 85, 10)
	.long R(13, 97, 22, 40), R(84, c6, 11, 20)
	.long R(85, 4a, 24, 7d), R(d2, bb, 3d, f8)
	.long R(ae, f9, 32, 11), R(c7, 29, a1, 6d)
	.long R(1d, 9e, 2f, 4b), R(dc, b2, 30, f3)
	.long R(0d, 86, 52, ec), R(77, c1, e3, d0)
	.long R(2b, b3, 16, 6c), R(a9, 70, b9, 99)
	.long R(11, 94, 48, fa), R(47, e9, 64, 22)
	.long R(a8, fc, 8c, c4), R(a0, f0, 3f, 1a)
	.long R(56, 7d, 2c, d8), R(22, 33, 90, ef)
	.long R(87, 49, 4e, c7), R(d9, 38, d1, c1)
	.long R(8c, ca, a2, fe), R(98, d4, 0b, 36)
	.long R(a6, f5, 81, cf), R(a5, 7a, de, 28)
	.long R(da, b7, 8e, 26), R(3f, ad, bf, a4)
	.long R(2c, 3a, 9d, e4), R(50, 78, 92, 0d)
	.long R(6a, 5f, cc, 9b), R(54, 7e, 46, 62)
	.long R(f6, 8d, 13, c2), R(90, d8, b8, e8)
	.long R(2e, 39, f7, 5e), R(82, c3, af, f5)
	.long R(9f, 5d, 80, be), R(69, d0, 93, 7c)
	.long R(6f, d5, 2d, a9), R(cf, 25, 12, b3)
	.long R(c8, ac, 99, 3b), R(10, 18, 7d, a7)
	.long R(e8, 9c, 63, 6e), R(db, 3b, bb, 7b)
	.long R(cd, 26, 78, 09), R(6e, 59, 18, f4)
	.long R(ec, 9a, b7, 01), R(83, 4f, 9a, a8)
	.long R(e6, 95, 6e, 65), R(aa, ff, e6, 7e)
	.long R(21, bc, cf, 08), R(ef, 15, e8, e6)
	.long R(ba, e7, 9b, d9), R(4a, 6f, 36, ce)
	.long R(ea, 9f, 09, d4), R(29, b0, 7c, d6)
	.long R(31, a4, b2, af), R(2a, 3f, 23, 31)
	.long R(c6, a5, 94, 30), R(35, a2, 66, c0)
	.long R(74, 4e, bc, 37), R(fc, 82, ca, a6)
	.long R(e0, 90, d0, b0), R(33, a7, d8, 15)
	.long R(f1, 04, 98, 4a), R(41, ec, da, f7)
	.long R(7f, cd, 50, 0e), R(17, 91, f6, 2f)
	.long R(76, 4d, d6, 8d), R(43, ef, b0, 4d)
	.long R(cc, aa, 4d, 54), R(e4, 96, 04, df)
	.long R(9e, d1, b5, e3), R(4c, 6a, 88, 1b)
	.long R(c1, 2c, 1f, b8), R(46, 65, 51, 7f)
	.long R(9d, 5e, ea, 04), R(01, 8c, 35, 5d)
	.long R(fa, 87, 74, 73), R(fb, 0b, 41, 2e)
	.long R(b3, 67, 1d, 5a), R(92, db, d2, 52)
	.long R(e9, 10, 56, 33), R(6d, d6, 47, 13)
	.long R(9a, d7, 61, 8c), R(37, a1, 0c, 7a)
	.long R(59, f8, 14, 8e), R(eb, 13, 3c, 89)
	.long R(ce, a9, 27, ee), R(b7, 61, c9, 35)
	.long R(e1, 1c, e5, ed), R(7a, 47, b1, 3c)
	.long R(9c, d2, df, 59), R(55, f2, 73, 3f)
	.long R(18, 14, ce, 79), R(73, c7, 37, bf)
	.long R(53, f7, cd, ea), R(5f, fd, aa, 5b)
	.long R(df, 3d, 6f, 14), R(78, 44, db, 86)
	.long R(ca, af, f3, 81), R(b9, 68, c4, 3e)
	.long R(38, 24, 34, 2c), R(c2, a3, 40, 5f)
	.long R(16, 1d, c3, 72), R(bc, e2, 25, 0c)
	.long R(28, 3c, 49, 8b), R(ff, 0d, 95, 41)
	.long R(39, a8, 01, 71), R(08, 0c, b3, de)
	.long R(d8, b4, e4, 9c), R(64, 56, c1, 90)
	.long R(7b, cb, 84, 61), R(d5, 32, b6, 70)
	.long R(48, 6c, 5c, 74), R(d0, b8, 57, 42)
.globl PPC_AES_4K_DECTAB2
PPC_AES_4K_DECTAB2:
/* decryption table, same as crypto_il_tab in crypto/aes-generic.c */
	.byte 0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
	.byte 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
	.byte 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
	.byte 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
	.byte 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
	.byte 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
	.byte 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
	.byte 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
	.byte 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
	.byte 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
	.byte 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
	.byte 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
	.byte 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
	.byte 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
	.byte 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
	.byte 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
	.byte 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
	.byte 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
	.byte 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
	.byte 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
	.byte 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
	.byte 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
	.byte 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
	.byte 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
	.byte 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
	.byte 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
	.byte 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
	.byte 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
	.byte 0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
	.byte 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
	.byte 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
	.byte 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
