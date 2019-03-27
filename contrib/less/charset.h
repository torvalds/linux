/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */

#define IS_ASCII_OCTET(c)   (((c) & 0x80) == 0)
#define IS_UTF8_TRAIL(c)    (((c) & 0xC0) == 0x80)
#define IS_UTF8_LEAD2(c)    (((c) & 0xE0) == 0xC0)
#define IS_UTF8_LEAD3(c)    (((c) & 0xF0) == 0xE0)
#define IS_UTF8_LEAD4(c)    (((c) & 0xF8) == 0xF0)
#define IS_UTF8_LEAD5(c)    (((c) & 0xFC) == 0xF8)
#define IS_UTF8_LEAD6(c)    (((c) & 0xFE) == 0xFC)
#define IS_UTF8_INVALID(c)  (((c) & 0xFE) == 0xFE)
#define IS_UTF8_LEAD(c)     (((c) & 0xC0) == 0xC0 && !IS_UTF8_INVALID(c))
