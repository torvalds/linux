// ------------------------------------------------------------------
// Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
// ------------------------------------------------------------------
//===================================================================
// Author(s): ="Atheros"
//===================================================================


#ifndef _USB_CAST_REG_REG_H_
#define _USB_CAST_REG_REG_H_

#define ENDP0_ADDRESS                            0x00000000
#define ENDP0_OFFSET                             0x00000000
#define ENDP0_CHGSETUP_MSB                       23
#define ENDP0_CHGSETUP_LSB                       23
#define ENDP0_CHGSETUP_MASK                      0x00800000
#define ENDP0_CHGSETUP_GET(x)                    (((x) & ENDP0_CHGSETUP_MASK) >> ENDP0_CHGSETUP_LSB)
#define ENDP0_CHGSETUP_SET(x)                    (((x) << ENDP0_CHGSETUP_LSB) & ENDP0_CHGSETUP_MASK)
#define ENDP0_DSTALL_MSB                         20
#define ENDP0_DSTALL_LSB                         20
#define ENDP0_DSTALL_MASK                        0x00100000
#define ENDP0_DSTALL_GET(x)                      (((x) & ENDP0_DSTALL_MASK) >> ENDP0_DSTALL_LSB)
#define ENDP0_DSTALL_SET(x)                      (((x) << ENDP0_DSTALL_LSB) & ENDP0_DSTALL_MASK)
#define ENDP0_HSNAK_MSB                          17
#define ENDP0_HSNAK_LSB                          17
#define ENDP0_HSNAK_MASK                         0x00020000
#define ENDP0_HSNAK_GET(x)                       (((x) & ENDP0_HSNAK_MASK) >> ENDP0_HSNAK_LSB)
#define ENDP0_HSNAK_SET(x)                       (((x) << ENDP0_HSNAK_LSB) & ENDP0_HSNAK_MASK)
#define ENDP0_STALL_MSB                          16
#define ENDP0_STALL_LSB                          16
#define ENDP0_STALL_MASK                         0x00010000
#define ENDP0_STALL_GET(x)                       (((x) & ENDP0_STALL_MASK) >> ENDP0_STALL_LSB)
#define ENDP0_STALL_SET(x)                       (((x) << ENDP0_STALL_LSB) & ENDP0_STALL_MASK)
#define ENDP0_MAXP_MSB                           7
#define ENDP0_MAXP_LSB                           0
#define ENDP0_MAXP_MASK                          0x000000ff
#define ENDP0_MAXP_GET(x)                        (((x) & ENDP0_MAXP_MASK) >> ENDP0_MAXP_LSB)
#define ENDP0_MAXP_SET(x)                        (((x) << ENDP0_MAXP_LSB) & ENDP0_MAXP_MASK)

#define OUT1ENDP_ADDRESS                         0x00000008
#define OUT1ENDP_OFFSET                          0x00000008
#define OUT1ENDP_ISOERR_MSB                      24
#define OUT1ENDP_ISOERR_LSB                      24
#define OUT1ENDP_ISOERR_MASK                     0x01000000
#define OUT1ENDP_ISOERR_GET(x)                   (((x) & OUT1ENDP_ISOERR_MASK) >> OUT1ENDP_ISOERR_LSB)
#define OUT1ENDP_ISOERR_SET(x)                   (((x) << OUT1ENDP_ISOERR_LSB) & OUT1ENDP_ISOERR_MASK)
#define OUT1ENDP_VAL_MSB                         23
#define OUT1ENDP_VAL_LSB                         23
#define OUT1ENDP_VAL_MASK                        0x00800000
#define OUT1ENDP_VAL_GET(x)                      (((x) & OUT1ENDP_VAL_MASK) >> OUT1ENDP_VAL_LSB)
#define OUT1ENDP_VAL_SET(x)                      (((x) << OUT1ENDP_VAL_LSB) & OUT1ENDP_VAL_MASK)
#define OUT1ENDP_STALL_MSB                       22
#define OUT1ENDP_STALL_LSB                       22
#define OUT1ENDP_STALL_MASK                      0x00400000
#define OUT1ENDP_STALL_GET(x)                    (((x) & OUT1ENDP_STALL_MASK) >> OUT1ENDP_STALL_LSB)
#define OUT1ENDP_STALL_SET(x)                    (((x) << OUT1ENDP_STALL_LSB) & OUT1ENDP_STALL_MASK)
#define OUT1ENDP_ISOD_MSB                        21
#define OUT1ENDP_ISOD_LSB                        20
#define OUT1ENDP_ISOD_MASK                       0x00300000
#define OUT1ENDP_ISOD_GET(x)                     (((x) & OUT1ENDP_ISOD_MASK) >> OUT1ENDP_ISOD_LSB)
#define OUT1ENDP_ISOD_SET(x)                     (((x) << OUT1ENDP_ISOD_LSB) & OUT1ENDP_ISOD_MASK)
#define OUT1ENDP_TYPE_MSB                        19
#define OUT1ENDP_TYPE_LSB                        18
#define OUT1ENDP_TYPE_MASK                       0x000c0000
#define OUT1ENDP_TYPE_GET(x)                     (((x) & OUT1ENDP_TYPE_MASK) >> OUT1ENDP_TYPE_LSB)
#define OUT1ENDP_TYPE_SET(x)                     (((x) << OUT1ENDP_TYPE_LSB) & OUT1ENDP_TYPE_MASK)
#define OUT1ENDP_MAXP_MSB                        10
#define OUT1ENDP_MAXP_LSB                        0
#define OUT1ENDP_MAXP_MASK                       0x000007ff
#define OUT1ENDP_MAXP_GET(x)                     (((x) & OUT1ENDP_MAXP_MASK) >> OUT1ENDP_MAXP_LSB)
#define OUT1ENDP_MAXP_SET(x)                     (((x) << OUT1ENDP_MAXP_LSB) & OUT1ENDP_MAXP_MASK)

#define IN1ENDP_ADDRESS                          0x0000000c
#define IN1ENDP_OFFSET                           0x0000000c
#define IN1ENDP_HCSET_MSB                        28
#define IN1ENDP_HCSET_LSB                        28
#define IN1ENDP_HCSET_MASK                       0x10000000
#define IN1ENDP_HCSET_GET(x)                     (((x) & IN1ENDP_HCSET_MASK) >> IN1ENDP_HCSET_LSB)
#define IN1ENDP_HCSET_SET(x)                     (((x) << IN1ENDP_HCSET_LSB) & IN1ENDP_HCSET_MASK)
#define IN1ENDP_ISOERR_MSB                       24
#define IN1ENDP_ISOERR_LSB                       24
#define IN1ENDP_ISOERR_MASK                      0x01000000
#define IN1ENDP_ISOERR_GET(x)                    (((x) & IN1ENDP_ISOERR_MASK) >> IN1ENDP_ISOERR_LSB)
#define IN1ENDP_ISOERR_SET(x)                    (((x) << IN1ENDP_ISOERR_LSB) & IN1ENDP_ISOERR_MASK)
#define IN1ENDP_VAL_MSB                          23
#define IN1ENDP_VAL_LSB                          23
#define IN1ENDP_VAL_MASK                         0x00800000
#define IN1ENDP_VAL_GET(x)                       (((x) & IN1ENDP_VAL_MASK) >> IN1ENDP_VAL_LSB)
#define IN1ENDP_VAL_SET(x)                       (((x) << IN1ENDP_VAL_LSB) & IN1ENDP_VAL_MASK)
#define IN1ENDP_STALL_MSB                        22
#define IN1ENDP_STALL_LSB                        22
#define IN1ENDP_STALL_MASK                       0x00400000
#define IN1ENDP_STALL_GET(x)                     (((x) & IN1ENDP_STALL_MASK) >> IN1ENDP_STALL_LSB)
#define IN1ENDP_STALL_SET(x)                     (((x) << IN1ENDP_STALL_LSB) & IN1ENDP_STALL_MASK)
#define IN1ENDP_ISOD_MSB                         21
#define IN1ENDP_ISOD_LSB                         20
#define IN1ENDP_ISOD_MASK                        0x00300000
#define IN1ENDP_ISOD_GET(x)                      (((x) & IN1ENDP_ISOD_MASK) >> IN1ENDP_ISOD_LSB)
#define IN1ENDP_ISOD_SET(x)                      (((x) << IN1ENDP_ISOD_LSB) & IN1ENDP_ISOD_MASK)
#define IN1ENDP_TYPE_MSB                         19
#define IN1ENDP_TYPE_LSB                         18
#define IN1ENDP_TYPE_MASK                        0x000c0000
#define IN1ENDP_TYPE_GET(x)                      (((x) & IN1ENDP_TYPE_MASK) >> IN1ENDP_TYPE_LSB)
#define IN1ENDP_TYPE_SET(x)                      (((x) << IN1ENDP_TYPE_LSB) & IN1ENDP_TYPE_MASK)
#define IN1ENDP_MAXP_MSB                         10
#define IN1ENDP_MAXP_LSB                         0
#define IN1ENDP_MAXP_MASK                        0x000007ff
#define IN1ENDP_MAXP_GET(x)                      (((x) & IN1ENDP_MAXP_MASK) >> IN1ENDP_MAXP_LSB)
#define IN1ENDP_MAXP_SET(x)                      (((x) << IN1ENDP_MAXP_LSB) & IN1ENDP_MAXP_MASK)

#define OUT2ENDP_ADDRESS                         0x00000010
#define OUT2ENDP_OFFSET                          0x00000010
#define OUT2ENDP_ISOERR_MSB                      24
#define OUT2ENDP_ISOERR_LSB                      24
#define OUT2ENDP_ISOERR_MASK                     0x01000000
#define OUT2ENDP_ISOERR_GET(x)                   (((x) & OUT2ENDP_ISOERR_MASK) >> OUT2ENDP_ISOERR_LSB)
#define OUT2ENDP_ISOERR_SET(x)                   (((x) << OUT2ENDP_ISOERR_LSB) & OUT2ENDP_ISOERR_MASK)
#define OUT2ENDP_VAL_MSB                         23
#define OUT2ENDP_VAL_LSB                         23
#define OUT2ENDP_VAL_MASK                        0x00800000
#define OUT2ENDP_VAL_GET(x)                      (((x) & OUT2ENDP_VAL_MASK) >> OUT2ENDP_VAL_LSB)
#define OUT2ENDP_VAL_SET(x)                      (((x) << OUT2ENDP_VAL_LSB) & OUT2ENDP_VAL_MASK)
#define OUT2ENDP_STALL_MSB                       22
#define OUT2ENDP_STALL_LSB                       22
#define OUT2ENDP_STALL_MASK                      0x00400000
#define OUT2ENDP_STALL_GET(x)                    (((x) & OUT2ENDP_STALL_MASK) >> OUT2ENDP_STALL_LSB)
#define OUT2ENDP_STALL_SET(x)                    (((x) << OUT2ENDP_STALL_LSB) & OUT2ENDP_STALL_MASK)
#define OUT2ENDP_ISOD_MSB                        21
#define OUT2ENDP_ISOD_LSB                        20
#define OUT2ENDP_ISOD_MASK                       0x00300000
#define OUT2ENDP_ISOD_GET(x)                     (((x) & OUT2ENDP_ISOD_MASK) >> OUT2ENDP_ISOD_LSB)
#define OUT2ENDP_ISOD_SET(x)                     (((x) << OUT2ENDP_ISOD_LSB) & OUT2ENDP_ISOD_MASK)
#define OUT2ENDP_TYPE_MSB                        19
#define OUT2ENDP_TYPE_LSB                        18
#define OUT2ENDP_TYPE_MASK                       0x000c0000
#define OUT2ENDP_TYPE_GET(x)                     (((x) & OUT2ENDP_TYPE_MASK) >> OUT2ENDP_TYPE_LSB)
#define OUT2ENDP_TYPE_SET(x)                     (((x) << OUT2ENDP_TYPE_LSB) & OUT2ENDP_TYPE_MASK)
#define OUT2ENDP_MAXP_MSB                        10
#define OUT2ENDP_MAXP_LSB                        0
#define OUT2ENDP_MAXP_MASK                       0x000007ff
#define OUT2ENDP_MAXP_GET(x)                     (((x) & OUT2ENDP_MAXP_MASK) >> OUT2ENDP_MAXP_LSB)
#define OUT2ENDP_MAXP_SET(x)                     (((x) << OUT2ENDP_MAXP_LSB) & OUT2ENDP_MAXP_MASK)

#define IN2ENDP_ADDRESS                          0x00000014
#define IN2ENDP_OFFSET                           0x00000014
#define IN2ENDP_HCSET_MSB                        28
#define IN2ENDP_HCSET_LSB                        28
#define IN2ENDP_HCSET_MASK                       0x10000000
#define IN2ENDP_HCSET_GET(x)                     (((x) & IN2ENDP_HCSET_MASK) >> IN2ENDP_HCSET_LSB)
#define IN2ENDP_HCSET_SET(x)                     (((x) << IN2ENDP_HCSET_LSB) & IN2ENDP_HCSET_MASK)
#define IN2ENDP_ISOERR_MSB                       24
#define IN2ENDP_ISOERR_LSB                       24
#define IN2ENDP_ISOERR_MASK                      0x01000000
#define IN2ENDP_ISOERR_GET(x)                    (((x) & IN2ENDP_ISOERR_MASK) >> IN2ENDP_ISOERR_LSB)
#define IN2ENDP_ISOERR_SET(x)                    (((x) << IN2ENDP_ISOERR_LSB) & IN2ENDP_ISOERR_MASK)
#define IN2ENDP_VAL_MSB                          23
#define IN2ENDP_VAL_LSB                          23
#define IN2ENDP_VAL_MASK                         0x00800000
#define IN2ENDP_VAL_GET(x)                       (((x) & IN2ENDP_VAL_MASK) >> IN2ENDP_VAL_LSB)
#define IN2ENDP_VAL_SET(x)                       (((x) << IN2ENDP_VAL_LSB) & IN2ENDP_VAL_MASK)
#define IN2ENDP_STALL_MSB                        22
#define IN2ENDP_STALL_LSB                        22
#define IN2ENDP_STALL_MASK                       0x00400000
#define IN2ENDP_STALL_GET(x)                     (((x) & IN2ENDP_STALL_MASK) >> IN2ENDP_STALL_LSB)
#define IN2ENDP_STALL_SET(x)                     (((x) << IN2ENDP_STALL_LSB) & IN2ENDP_STALL_MASK)
#define IN2ENDP_ISOD_MSB                         21
#define IN2ENDP_ISOD_LSB                         20
#define IN2ENDP_ISOD_MASK                        0x00300000
#define IN2ENDP_ISOD_GET(x)                      (((x) & IN2ENDP_ISOD_MASK) >> IN2ENDP_ISOD_LSB)
#define IN2ENDP_ISOD_SET(x)                      (((x) << IN2ENDP_ISOD_LSB) & IN2ENDP_ISOD_MASK)
#define IN2ENDP_TYPE_MSB                         19
#define IN2ENDP_TYPE_LSB                         18
#define IN2ENDP_TYPE_MASK                        0x000c0000
#define IN2ENDP_TYPE_GET(x)                      (((x) & IN2ENDP_TYPE_MASK) >> IN2ENDP_TYPE_LSB)
#define IN2ENDP_TYPE_SET(x)                      (((x) << IN2ENDP_TYPE_LSB) & IN2ENDP_TYPE_MASK)
#define IN2ENDP_MAXP_MSB                         10
#define IN2ENDP_MAXP_LSB                         0
#define IN2ENDP_MAXP_MASK                        0x000007ff
#define IN2ENDP_MAXP_GET(x)                      (((x) & IN2ENDP_MAXP_MASK) >> IN2ENDP_MAXP_LSB)
#define IN2ENDP_MAXP_SET(x)                      (((x) << IN2ENDP_MAXP_LSB) & IN2ENDP_MAXP_MASK)

#define OUT3ENDP_ADDRESS                         0x00000018
#define OUT3ENDP_OFFSET                          0x00000018
#define OUT3ENDP_ISOERR_MSB                      24
#define OUT3ENDP_ISOERR_LSB                      24
#define OUT3ENDP_ISOERR_MASK                     0x01000000
#define OUT3ENDP_ISOERR_GET(x)                   (((x) & OUT3ENDP_ISOERR_MASK) >> OUT3ENDP_ISOERR_LSB)
#define OUT3ENDP_ISOERR_SET(x)                   (((x) << OUT3ENDP_ISOERR_LSB) & OUT3ENDP_ISOERR_MASK)
#define OUT3ENDP_VAL_MSB                         23
#define OUT3ENDP_VAL_LSB                         23
#define OUT3ENDP_VAL_MASK                        0x00800000
#define OUT3ENDP_VAL_GET(x)                      (((x) & OUT3ENDP_VAL_MASK) >> OUT3ENDP_VAL_LSB)
#define OUT3ENDP_VAL_SET(x)                      (((x) << OUT3ENDP_VAL_LSB) & OUT3ENDP_VAL_MASK)
#define OUT3ENDP_STALL_MSB                       22
#define OUT3ENDP_STALL_LSB                       22
#define OUT3ENDP_STALL_MASK                      0x00400000
#define OUT3ENDP_STALL_GET(x)                    (((x) & OUT3ENDP_STALL_MASK) >> OUT3ENDP_STALL_LSB)
#define OUT3ENDP_STALL_SET(x)                    (((x) << OUT3ENDP_STALL_LSB) & OUT3ENDP_STALL_MASK)
#define OUT3ENDP_ISOD_MSB                        21
#define OUT3ENDP_ISOD_LSB                        20
#define OUT3ENDP_ISOD_MASK                       0x00300000
#define OUT3ENDP_ISOD_GET(x)                     (((x) & OUT3ENDP_ISOD_MASK) >> OUT3ENDP_ISOD_LSB)
#define OUT3ENDP_ISOD_SET(x)                     (((x) << OUT3ENDP_ISOD_LSB) & OUT3ENDP_ISOD_MASK)
#define OUT3ENDP_TYPE_MSB                        19
#define OUT3ENDP_TYPE_LSB                        18
#define OUT3ENDP_TYPE_MASK                       0x000c0000
#define OUT3ENDP_TYPE_GET(x)                     (((x) & OUT3ENDP_TYPE_MASK) >> OUT3ENDP_TYPE_LSB)
#define OUT3ENDP_TYPE_SET(x)                     (((x) << OUT3ENDP_TYPE_LSB) & OUT3ENDP_TYPE_MASK)
#define OUT3ENDP_MAXP_MSB                        10
#define OUT3ENDP_MAXP_LSB                        0
#define OUT3ENDP_MAXP_MASK                       0x000007ff
#define OUT3ENDP_MAXP_GET(x)                     (((x) & OUT3ENDP_MAXP_MASK) >> OUT3ENDP_MAXP_LSB)
#define OUT3ENDP_MAXP_SET(x)                     (((x) << OUT3ENDP_MAXP_LSB) & OUT3ENDP_MAXP_MASK)

#define IN3ENDP_ADDRESS                          0x0000001c
#define IN3ENDP_OFFSET                           0x0000001c
#define IN3ENDP_HCSET_MSB                        28
#define IN3ENDP_HCSET_LSB                        28
#define IN3ENDP_HCSET_MASK                       0x10000000
#define IN3ENDP_HCSET_GET(x)                     (((x) & IN3ENDP_HCSET_MASK) >> IN3ENDP_HCSET_LSB)
#define IN3ENDP_HCSET_SET(x)                     (((x) << IN3ENDP_HCSET_LSB) & IN3ENDP_HCSET_MASK)
#define IN3ENDP_ISOERR_MSB                       24
#define IN3ENDP_ISOERR_LSB                       24
#define IN3ENDP_ISOERR_MASK                      0x01000000
#define IN3ENDP_ISOERR_GET(x)                    (((x) & IN3ENDP_ISOERR_MASK) >> IN3ENDP_ISOERR_LSB)
#define IN3ENDP_ISOERR_SET(x)                    (((x) << IN3ENDP_ISOERR_LSB) & IN3ENDP_ISOERR_MASK)
#define IN3ENDP_VAL_MSB                          23
#define IN3ENDP_VAL_LSB                          23
#define IN3ENDP_VAL_MASK                         0x00800000
#define IN3ENDP_VAL_GET(x)                       (((x) & IN3ENDP_VAL_MASK) >> IN3ENDP_VAL_LSB)
#define IN3ENDP_VAL_SET(x)                       (((x) << IN3ENDP_VAL_LSB) & IN3ENDP_VAL_MASK)
#define IN3ENDP_STALL_MSB                        22
#define IN3ENDP_STALL_LSB                        22
#define IN3ENDP_STALL_MASK                       0x00400000
#define IN3ENDP_STALL_GET(x)                     (((x) & IN3ENDP_STALL_MASK) >> IN3ENDP_STALL_LSB)
#define IN3ENDP_STALL_SET(x)                     (((x) << IN3ENDP_STALL_LSB) & IN3ENDP_STALL_MASK)
#define IN3ENDP_ISOD_MSB                         21
#define IN3ENDP_ISOD_LSB                         20
#define IN3ENDP_ISOD_MASK                        0x00300000
#define IN3ENDP_ISOD_GET(x)                      (((x) & IN3ENDP_ISOD_MASK) >> IN3ENDP_ISOD_LSB)
#define IN3ENDP_ISOD_SET(x)                      (((x) << IN3ENDP_ISOD_LSB) & IN3ENDP_ISOD_MASK)
#define IN3ENDP_TYPE_MSB                         19
#define IN3ENDP_TYPE_LSB                         18
#define IN3ENDP_TYPE_MASK                        0x000c0000
#define IN3ENDP_TYPE_GET(x)                      (((x) & IN3ENDP_TYPE_MASK) >> IN3ENDP_TYPE_LSB)
#define IN3ENDP_TYPE_SET(x)                      (((x) << IN3ENDP_TYPE_LSB) & IN3ENDP_TYPE_MASK)
#define IN3ENDP_MAXP_MSB                         10
#define IN3ENDP_MAXP_LSB                         0
#define IN3ENDP_MAXP_MASK                        0x000007ff
#define IN3ENDP_MAXP_GET(x)                      (((x) & IN3ENDP_MAXP_MASK) >> IN3ENDP_MAXP_LSB)
#define IN3ENDP_MAXP_SET(x)                      (((x) << IN3ENDP_MAXP_LSB) & IN3ENDP_MAXP_MASK)

#define OUT4ENDP_ADDRESS                         0x00000020
#define OUT4ENDP_OFFSET                          0x00000020
#define OUT4ENDP_ISOERR_MSB                      24
#define OUT4ENDP_ISOERR_LSB                      24
#define OUT4ENDP_ISOERR_MASK                     0x01000000
#define OUT4ENDP_ISOERR_GET(x)                   (((x) & OUT4ENDP_ISOERR_MASK) >> OUT4ENDP_ISOERR_LSB)
#define OUT4ENDP_ISOERR_SET(x)                   (((x) << OUT4ENDP_ISOERR_LSB) & OUT4ENDP_ISOERR_MASK)
#define OUT4ENDP_VAL_MSB                         23
#define OUT4ENDP_VAL_LSB                         23
#define OUT4ENDP_VAL_MASK                        0x00800000
#define OUT4ENDP_VAL_GET(x)                      (((x) & OUT4ENDP_VAL_MASK) >> OUT4ENDP_VAL_LSB)
#define OUT4ENDP_VAL_SET(x)                      (((x) << OUT4ENDP_VAL_LSB) & OUT4ENDP_VAL_MASK)
#define OUT4ENDP_STALL_MSB                       22
#define OUT4ENDP_STALL_LSB                       22
#define OUT4ENDP_STALL_MASK                      0x00400000
#define OUT4ENDP_STALL_GET(x)                    (((x) & OUT4ENDP_STALL_MASK) >> OUT4ENDP_STALL_LSB)
#define OUT4ENDP_STALL_SET(x)                    (((x) << OUT4ENDP_STALL_LSB) & OUT4ENDP_STALL_MASK)
#define OUT4ENDP_ISOD_MSB                        21
#define OUT4ENDP_ISOD_LSB                        20
#define OUT4ENDP_ISOD_MASK                       0x00300000
#define OUT4ENDP_ISOD_GET(x)                     (((x) & OUT4ENDP_ISOD_MASK) >> OUT4ENDP_ISOD_LSB)
#define OUT4ENDP_ISOD_SET(x)                     (((x) << OUT4ENDP_ISOD_LSB) & OUT4ENDP_ISOD_MASK)
#define OUT4ENDP_TYPE_MSB                        19
#define OUT4ENDP_TYPE_LSB                        18
#define OUT4ENDP_TYPE_MASK                       0x000c0000
#define OUT4ENDP_TYPE_GET(x)                     (((x) & OUT4ENDP_TYPE_MASK) >> OUT4ENDP_TYPE_LSB)
#define OUT4ENDP_TYPE_SET(x)                     (((x) << OUT4ENDP_TYPE_LSB) & OUT4ENDP_TYPE_MASK)
#define OUT4ENDP_MAXP_MSB                        10
#define OUT4ENDP_MAXP_LSB                        0
#define OUT4ENDP_MAXP_MASK                       0x000007ff
#define OUT4ENDP_MAXP_GET(x)                     (((x) & OUT4ENDP_MAXP_MASK) >> OUT4ENDP_MAXP_LSB)
#define OUT4ENDP_MAXP_SET(x)                     (((x) << OUT4ENDP_MAXP_LSB) & OUT4ENDP_MAXP_MASK)

#define IN4ENDP_ADDRESS                          0x00000024
#define IN4ENDP_OFFSET                           0x00000024
#define IN4ENDP_HCSET_MSB                        28
#define IN4ENDP_HCSET_LSB                        28
#define IN4ENDP_HCSET_MASK                       0x10000000
#define IN4ENDP_HCSET_GET(x)                     (((x) & IN4ENDP_HCSET_MASK) >> IN4ENDP_HCSET_LSB)
#define IN4ENDP_HCSET_SET(x)                     (((x) << IN4ENDP_HCSET_LSB) & IN4ENDP_HCSET_MASK)
#define IN4ENDP_ISOERR_MSB                       24
#define IN4ENDP_ISOERR_LSB                       24
#define IN4ENDP_ISOERR_MASK                      0x01000000
#define IN4ENDP_ISOERR_GET(x)                    (((x) & IN4ENDP_ISOERR_MASK) >> IN4ENDP_ISOERR_LSB)
#define IN4ENDP_ISOERR_SET(x)                    (((x) << IN4ENDP_ISOERR_LSB) & IN4ENDP_ISOERR_MASK)
#define IN4ENDP_VAL_MSB                          23
#define IN4ENDP_VAL_LSB                          23
#define IN4ENDP_VAL_MASK                         0x00800000
#define IN4ENDP_VAL_GET(x)                       (((x) & IN4ENDP_VAL_MASK) >> IN4ENDP_VAL_LSB)
#define IN4ENDP_VAL_SET(x)                       (((x) << IN4ENDP_VAL_LSB) & IN4ENDP_VAL_MASK)
#define IN4ENDP_STALL_MSB                        22
#define IN4ENDP_STALL_LSB                        22
#define IN4ENDP_STALL_MASK                       0x00400000
#define IN4ENDP_STALL_GET(x)                     (((x) & IN4ENDP_STALL_MASK) >> IN4ENDP_STALL_LSB)
#define IN4ENDP_STALL_SET(x)                     (((x) << IN4ENDP_STALL_LSB) & IN4ENDP_STALL_MASK)
#define IN4ENDP_ISOD_MSB                         21
#define IN4ENDP_ISOD_LSB                         20
#define IN4ENDP_ISOD_MASK                        0x00300000
#define IN4ENDP_ISOD_GET(x)                      (((x) & IN4ENDP_ISOD_MASK) >> IN4ENDP_ISOD_LSB)
#define IN4ENDP_ISOD_SET(x)                      (((x) << IN4ENDP_ISOD_LSB) & IN4ENDP_ISOD_MASK)
#define IN4ENDP_TYPE_MSB                         19
#define IN4ENDP_TYPE_LSB                         18
#define IN4ENDP_TYPE_MASK                        0x000c0000
#define IN4ENDP_TYPE_GET(x)                      (((x) & IN4ENDP_TYPE_MASK) >> IN4ENDP_TYPE_LSB)
#define IN4ENDP_TYPE_SET(x)                      (((x) << IN4ENDP_TYPE_LSB) & IN4ENDP_TYPE_MASK)
#define IN4ENDP_MAXP_MSB                         10
#define IN4ENDP_MAXP_LSB                         0
#define IN4ENDP_MAXP_MASK                        0x000007ff
#define IN4ENDP_MAXP_GET(x)                      (((x) & IN4ENDP_MAXP_MASK) >> IN4ENDP_MAXP_LSB)
#define IN4ENDP_MAXP_SET(x)                      (((x) << IN4ENDP_MAXP_LSB) & IN4ENDP_MAXP_MASK)

#define OUT5ENDP_ADDRESS                         0x00000028
#define OUT5ENDP_OFFSET                          0x00000028
#define OUT5ENDP_ISOERR_MSB                      24
#define OUT5ENDP_ISOERR_LSB                      24
#define OUT5ENDP_ISOERR_MASK                     0x01000000
#define OUT5ENDP_ISOERR_GET(x)                   (((x) & OUT5ENDP_ISOERR_MASK) >> OUT5ENDP_ISOERR_LSB)
#define OUT5ENDP_ISOERR_SET(x)                   (((x) << OUT5ENDP_ISOERR_LSB) & OUT5ENDP_ISOERR_MASK)
#define OUT5ENDP_VAL_MSB                         23
#define OUT5ENDP_VAL_LSB                         23
#define OUT5ENDP_VAL_MASK                        0x00800000
#define OUT5ENDP_VAL_GET(x)                      (((x) & OUT5ENDP_VAL_MASK) >> OUT5ENDP_VAL_LSB)
#define OUT5ENDP_VAL_SET(x)                      (((x) << OUT5ENDP_VAL_LSB) & OUT5ENDP_VAL_MASK)
#define OUT5ENDP_STALL_MSB                       22
#define OUT5ENDP_STALL_LSB                       22
#define OUT5ENDP_STALL_MASK                      0x00400000
#define OUT5ENDP_STALL_GET(x)                    (((x) & OUT5ENDP_STALL_MASK) >> OUT5ENDP_STALL_LSB)
#define OUT5ENDP_STALL_SET(x)                    (((x) << OUT5ENDP_STALL_LSB) & OUT5ENDP_STALL_MASK)
#define OUT5ENDP_ISOD_MSB                        21
#define OUT5ENDP_ISOD_LSB                        20
#define OUT5ENDP_ISOD_MASK                       0x00300000
#define OUT5ENDP_ISOD_GET(x)                     (((x) & OUT5ENDP_ISOD_MASK) >> OUT5ENDP_ISOD_LSB)
#define OUT5ENDP_ISOD_SET(x)                     (((x) << OUT5ENDP_ISOD_LSB) & OUT5ENDP_ISOD_MASK)
#define OUT5ENDP_TYPE_MSB                        19
#define OUT5ENDP_TYPE_LSB                        18
#define OUT5ENDP_TYPE_MASK                       0x000c0000
#define OUT5ENDP_TYPE_GET(x)                     (((x) & OUT5ENDP_TYPE_MASK) >> OUT5ENDP_TYPE_LSB)
#define OUT5ENDP_TYPE_SET(x)                     (((x) << OUT5ENDP_TYPE_LSB) & OUT5ENDP_TYPE_MASK)
#define OUT5ENDP_MAXP_MSB                        10
#define OUT5ENDP_MAXP_LSB                        0
#define OUT5ENDP_MAXP_MASK                       0x000007ff
#define OUT5ENDP_MAXP_GET(x)                     (((x) & OUT5ENDP_MAXP_MASK) >> OUT5ENDP_MAXP_LSB)
#define OUT5ENDP_MAXP_SET(x)                     (((x) << OUT5ENDP_MAXP_LSB) & OUT5ENDP_MAXP_MASK)

#define IN5ENDP_ADDRESS                          0x0000002c
#define IN5ENDP_OFFSET                           0x0000002c
#define IN5ENDP_HCSET_MSB                        28
#define IN5ENDP_HCSET_LSB                        28
#define IN5ENDP_HCSET_MASK                       0x10000000
#define IN5ENDP_HCSET_GET(x)                     (((x) & IN5ENDP_HCSET_MASK) >> IN5ENDP_HCSET_LSB)
#define IN5ENDP_HCSET_SET(x)                     (((x) << IN5ENDP_HCSET_LSB) & IN5ENDP_HCSET_MASK)
#define IN5ENDP_ISOERR_MSB                       24
#define IN5ENDP_ISOERR_LSB                       24
#define IN5ENDP_ISOERR_MASK                      0x01000000
#define IN5ENDP_ISOERR_GET(x)                    (((x) & IN5ENDP_ISOERR_MASK) >> IN5ENDP_ISOERR_LSB)
#define IN5ENDP_ISOERR_SET(x)                    (((x) << IN5ENDP_ISOERR_LSB) & IN5ENDP_ISOERR_MASK)
#define IN5ENDP_VAL_MSB                          23
#define IN5ENDP_VAL_LSB                          23
#define IN5ENDP_VAL_MASK                         0x00800000
#define IN5ENDP_VAL_GET(x)                       (((x) & IN5ENDP_VAL_MASK) >> IN5ENDP_VAL_LSB)
#define IN5ENDP_VAL_SET(x)                       (((x) << IN5ENDP_VAL_LSB) & IN5ENDP_VAL_MASK)
#define IN5ENDP_STALL_MSB                        22
#define IN5ENDP_STALL_LSB                        22
#define IN5ENDP_STALL_MASK                       0x00400000
#define IN5ENDP_STALL_GET(x)                     (((x) & IN5ENDP_STALL_MASK) >> IN5ENDP_STALL_LSB)
#define IN5ENDP_STALL_SET(x)                     (((x) << IN5ENDP_STALL_LSB) & IN5ENDP_STALL_MASK)
#define IN5ENDP_ISOD_MSB                         21
#define IN5ENDP_ISOD_LSB                         20
#define IN5ENDP_ISOD_MASK                        0x00300000
#define IN5ENDP_ISOD_GET(x)                      (((x) & IN5ENDP_ISOD_MASK) >> IN5ENDP_ISOD_LSB)
#define IN5ENDP_ISOD_SET(x)                      (((x) << IN5ENDP_ISOD_LSB) & IN5ENDP_ISOD_MASK)
#define IN5ENDP_TYPE_MSB                         19
#define IN5ENDP_TYPE_LSB                         18
#define IN5ENDP_TYPE_MASK                        0x000c0000
#define IN5ENDP_TYPE_GET(x)                      (((x) & IN5ENDP_TYPE_MASK) >> IN5ENDP_TYPE_LSB)
#define IN5ENDP_TYPE_SET(x)                      (((x) << IN5ENDP_TYPE_LSB) & IN5ENDP_TYPE_MASK)
#define IN5ENDP_MAXP_MSB                         10
#define IN5ENDP_MAXP_LSB                         0
#define IN5ENDP_MAXP_MASK                        0x000007ff
#define IN5ENDP_MAXP_GET(x)                      (((x) & IN5ENDP_MAXP_MASK) >> IN5ENDP_MAXP_LSB)
#define IN5ENDP_MAXP_SET(x)                      (((x) << IN5ENDP_MAXP_LSB) & IN5ENDP_MAXP_MASK)

#define USBMODESTATUS_ADDRESS                    0x0000008c
#define USBMODESTATUS_OFFSET                     0x0000008c
#define USBMODESTATUS_DEVICE_MSB                 5
#define USBMODESTATUS_DEVICE_LSB                 5
#define USBMODESTATUS_DEVICE_MASK                0x00000020
#define USBMODESTATUS_DEVICE_GET(x)              (((x) & USBMODESTATUS_DEVICE_MASK) >> USBMODESTATUS_DEVICE_LSB)
#define USBMODESTATUS_DEVICE_SET(x)              (((x) << USBMODESTATUS_DEVICE_LSB) & USBMODESTATUS_DEVICE_MASK)
#define USBMODESTATUS_HOST_MSB                   4
#define USBMODESTATUS_HOST_LSB                   4
#define USBMODESTATUS_HOST_MASK                  0x00000010
#define USBMODESTATUS_HOST_GET(x)                (((x) & USBMODESTATUS_HOST_MASK) >> USBMODESTATUS_HOST_LSB)
#define USBMODESTATUS_HOST_SET(x)                (((x) << USBMODESTATUS_HOST_LSB) & USBMODESTATUS_HOST_MASK)
#define USBMODESTATUS_HS_MSB                     2
#define USBMODESTATUS_HS_LSB                     2
#define USBMODESTATUS_HS_MASK                    0x00000004
#define USBMODESTATUS_HS_GET(x)                  (((x) & USBMODESTATUS_HS_MASK) >> USBMODESTATUS_HS_LSB)
#define USBMODESTATUS_HS_SET(x)                  (((x) << USBMODESTATUS_HS_LSB) & USBMODESTATUS_HS_MASK)
#define USBMODESTATUS_FS_MSB                     1
#define USBMODESTATUS_FS_LSB                     1
#define USBMODESTATUS_FS_MASK                    0x00000002
#define USBMODESTATUS_FS_GET(x)                  (((x) & USBMODESTATUS_FS_MASK) >> USBMODESTATUS_FS_LSB)
#define USBMODESTATUS_FS_SET(x)                  (((x) << USBMODESTATUS_FS_LSB) & USBMODESTATUS_FS_MASK)
#define USBMODESTATUS_LS_MSB                     0
#define USBMODESTATUS_LS_LSB                     0
#define USBMODESTATUS_LS_MASK                    0x00000001
#define USBMODESTATUS_LS_GET(x)                  (((x) & USBMODESTATUS_LS_MASK) >> USBMODESTATUS_LS_LSB)
#define USBMODESTATUS_LS_SET(x)                  (((x) << USBMODESTATUS_LS_LSB) & USBMODESTATUS_LS_MASK)

#define EPIRQ_ADDRESS                            0x00000188
#define EPIRQ_OFFSET                             0x00000188
#define EPIRQ_OUTIRQ_MSB                         31
#define EPIRQ_OUTIRQ_LSB                         16
#define EPIRQ_OUTIRQ_MASK                        0xffff0000
#define EPIRQ_OUTIRQ_GET(x)                      (((x) & EPIRQ_OUTIRQ_MASK) >> EPIRQ_OUTIRQ_LSB)
#define EPIRQ_OUTIRQ_SET(x)                      (((x) << EPIRQ_OUTIRQ_LSB) & EPIRQ_OUTIRQ_MASK)
#define EPIRQ_INIRQ_MSB                          15
#define EPIRQ_INIRQ_LSB                          0
#define EPIRQ_INIRQ_MASK                         0x0000ffff
#define EPIRQ_INIRQ_GET(x)                       (((x) & EPIRQ_INIRQ_MASK) >> EPIRQ_INIRQ_LSB)
#define EPIRQ_INIRQ_SET(x)                       (((x) << EPIRQ_INIRQ_LSB) & EPIRQ_INIRQ_MASK)

#define USBIRQ_ADDRESS                           0x0000018c
#define USBIRQ_OFFSET                            0x0000018c
#define USBIRQ_OUTPNGIRQ_MSB                     31
#define USBIRQ_OUTPNGIRQ_LSB                     16
#define USBIRQ_OUTPNGIRQ_MASK                    0xffff0000
#define USBIRQ_OUTPNGIRQ_GET(x)                  (((x) & USBIRQ_OUTPNGIRQ_MASK) >> USBIRQ_OUTPNGIRQ_LSB)
#define USBIRQ_OUTPNGIRQ_SET(x)                  (((x) << USBIRQ_OUTPNGIRQ_LSB) & USBIRQ_OUTPNGIRQ_MASK)
#define USBIRQ_LPMIR_MSB                         7
#define USBIRQ_LPMIR_LSB                         7
#define USBIRQ_LPMIR_MASK                        0x00000080
#define USBIRQ_LPMIR_GET(x)                      (((x) & USBIRQ_LPMIR_MASK) >> USBIRQ_LPMIR_LSB)
#define USBIRQ_LPMIR_SET(x)                      (((x) << USBIRQ_LPMIR_LSB) & USBIRQ_LPMIR_MASK)
#define USBIRQ_OVERFLOWIR_MSB                    6
#define USBIRQ_OVERFLOWIR_LSB                    6
#define USBIRQ_OVERFLOWIR_MASK                   0x00000040
#define USBIRQ_OVERFLOWIR_GET(x)                 (((x) & USBIRQ_OVERFLOWIR_MASK) >> USBIRQ_OVERFLOWIR_LSB)
#define USBIRQ_OVERFLOWIR_SET(x)                 (((x) << USBIRQ_OVERFLOWIR_LSB) & USBIRQ_OVERFLOWIR_MASK)
#define USBIRQ_HSPEEDIR_MSB                      5
#define USBIRQ_HSPEEDIR_LSB                      5
#define USBIRQ_HSPEEDIR_MASK                     0x00000020
#define USBIRQ_HSPEEDIR_GET(x)                   (((x) & USBIRQ_HSPEEDIR_MASK) >> USBIRQ_HSPEEDIR_LSB)
#define USBIRQ_HSPEEDIR_SET(x)                   (((x) << USBIRQ_HSPEEDIR_LSB) & USBIRQ_HSPEEDIR_MASK)
#define USBIRQ_URESIR_MSB                        4
#define USBIRQ_URESIR_LSB                        4
#define USBIRQ_URESIR_MASK                       0x00000010
#define USBIRQ_URESIR_GET(x)                     (((x) & USBIRQ_URESIR_MASK) >> USBIRQ_URESIR_LSB)
#define USBIRQ_URESIR_SET(x)                     (((x) << USBIRQ_URESIR_LSB) & USBIRQ_URESIR_MASK)
#define USBIRQ_SUSPIR_MSB                        3
#define USBIRQ_SUSPIR_LSB                        3
#define USBIRQ_SUSPIR_MASK                       0x00000008
#define USBIRQ_SUSPIR_GET(x)                     (((x) & USBIRQ_SUSPIR_MASK) >> USBIRQ_SUSPIR_LSB)
#define USBIRQ_SUSPIR_SET(x)                     (((x) << USBIRQ_SUSPIR_LSB) & USBIRQ_SUSPIR_MASK)
#define USBIRQ_SUTOKIR_MSB                       2
#define USBIRQ_SUTOKIR_LSB                       2
#define USBIRQ_SUTOKIR_MASK                      0x00000004
#define USBIRQ_SUTOKIR_GET(x)                    (((x) & USBIRQ_SUTOKIR_MASK) >> USBIRQ_SUTOKIR_LSB)
#define USBIRQ_SUTOKIR_SET(x)                    (((x) << USBIRQ_SUTOKIR_LSB) & USBIRQ_SUTOKIR_MASK)
#define USBIRQ_SOFIR_MSB                         1
#define USBIRQ_SOFIR_LSB                         1
#define USBIRQ_SOFIR_MASK                        0x00000002
#define USBIRQ_SOFIR_GET(x)                      (((x) & USBIRQ_SOFIR_MASK) >> USBIRQ_SOFIR_LSB)
#define USBIRQ_SOFIR_SET(x)                      (((x) << USBIRQ_SOFIR_LSB) & USBIRQ_SOFIR_MASK)
#define USBIRQ_SUDAVIR_MSB                       0
#define USBIRQ_SUDAVIR_LSB                       0
#define USBIRQ_SUDAVIR_MASK                      0x00000001
#define USBIRQ_SUDAVIR_GET(x)                    (((x) & USBIRQ_SUDAVIR_MASK) >> USBIRQ_SUDAVIR_LSB)
#define USBIRQ_SUDAVIR_SET(x)                    (((x) << USBIRQ_SUDAVIR_LSB) & USBIRQ_SUDAVIR_MASK)

#define EPIEN_ADDRESS                            0x00000194
#define EPIEN_OFFSET                             0x00000194
#define EPIEN_OUTIEN_MSB                         31
#define EPIEN_OUTIEN_LSB                         16
#define EPIEN_OUTIEN_MASK                        0xffff0000
#define EPIEN_OUTIEN_GET(x)                      (((x) & EPIEN_OUTIEN_MASK) >> EPIEN_OUTIEN_LSB)
#define EPIEN_OUTIEN_SET(x)                      (((x) << EPIEN_OUTIEN_LSB) & EPIEN_OUTIEN_MASK)
#define EPIEN_INIEN_MSB                          15
#define EPIEN_INIEN_LSB                          0
#define EPIEN_INIEN_MASK                         0x0000ffff
#define EPIEN_INIEN_GET(x)                       (((x) & EPIEN_INIEN_MASK) >> EPIEN_INIEN_LSB)
#define EPIEN_INIEN_SET(x)                       (((x) << EPIEN_INIEN_LSB) & EPIEN_INIEN_MASK)

#define PIEN_ADDRESS                             0x00000198
#define PIEN_OFFSET                              0x00000198
#define PIEN_OUTPIE_MSB                          31
#define PIEN_OUTPIE_LSB                          16
#define PIEN_OUTPIE_MASK                         0xffff0000
#define PIEN_OUTPIE_GET(x)                       (((x) & PIEN_OUTPIE_MASK) >> PIEN_OUTPIE_LSB)
#define PIEN_OUTPIE_SET(x)                       (((x) << PIEN_OUTPIE_LSB) & PIEN_OUTPIE_MASK)
#define PIEN_LPMIE_MSB                           7
#define PIEN_LPMIE_LSB                           7
#define PIEN_LPMIE_MASK                          0x00000080
#define PIEN_LPMIE_GET(x)                        (((x) & PIEN_LPMIE_MASK) >> PIEN_LPMIE_LSB)
#define PIEN_LPMIE_SET(x)                        (((x) << PIEN_LPMIE_LSB) & PIEN_LPMIE_MASK)
#define PIEN_OVERFLOIE_MSB                       6
#define PIEN_OVERFLOIE_LSB                       6
#define PIEN_OVERFLOIE_MASK                      0x00000040
#define PIEN_OVERFLOIE_GET(x)                    (((x) & PIEN_OVERFLOIE_MASK) >> PIEN_OVERFLOIE_LSB)
#define PIEN_OVERFLOIE_SET(x)                    (((x) << PIEN_OVERFLOIE_LSB) & PIEN_OVERFLOIE_MASK)
#define PIEN_HSPEEDIE_MSB                        5
#define PIEN_HSPEEDIE_LSB                        5
#define PIEN_HSPEEDIE_MASK                       0x00000020
#define PIEN_HSPEEDIE_GET(x)                     (((x) & PIEN_HSPEEDIE_MASK) >> PIEN_HSPEEDIE_LSB)
#define PIEN_HSPEEDIE_SET(x)                     (((x) << PIEN_HSPEEDIE_LSB) & PIEN_HSPEEDIE_MASK)
#define PIEN_URESIE_MSB                          4
#define PIEN_URESIE_LSB                          4
#define PIEN_URESIE_MASK                         0x00000010
#define PIEN_URESIE_GET(x)                       (((x) & PIEN_URESIE_MASK) >> PIEN_URESIE_LSB)
#define PIEN_URESIE_SET(x)                       (((x) << PIEN_URESIE_LSB) & PIEN_URESIE_MASK)
#define PIEN_SUSPIE_MSB                          3
#define PIEN_SUSPIE_LSB                          3
#define PIEN_SUSPIE_MASK                         0x00000008
#define PIEN_SUSPIE_GET(x)                       (((x) & PIEN_SUSPIE_MASK) >> PIEN_SUSPIE_LSB)
#define PIEN_SUSPIE_SET(x)                       (((x) << PIEN_SUSPIE_LSB) & PIEN_SUSPIE_MASK)
#define PIEN_SUTOKIE_MSB                         2
#define PIEN_SUTOKIE_LSB                         2
#define PIEN_SUTOKIE_MASK                        0x00000004
#define PIEN_SUTOKIE_GET(x)                      (((x) & PIEN_SUTOKIE_MASK) >> PIEN_SUTOKIE_LSB)
#define PIEN_SUTOKIE_SET(x)                      (((x) << PIEN_SUTOKIE_LSB) & PIEN_SUTOKIE_MASK)
#define PIEN_SOFIE_MSB                           1
#define PIEN_SOFIE_LSB                           1
#define PIEN_SOFIE_MASK                          0x00000002
#define PIEN_SOFIE_GET(x)                        (((x) & PIEN_SOFIE_MASK) >> PIEN_SOFIE_LSB)
#define PIEN_SOFIE_SET(x)                        (((x) << PIEN_SOFIE_LSB) & PIEN_SOFIE_MASK)
#define PIEN_SUDAVIE_MSB                         0
#define PIEN_SUDAVIE_LSB                         0
#define PIEN_SUDAVIE_MASK                        0x00000001
#define PIEN_SUDAVIE_GET(x)                      (((x) & PIEN_SUDAVIE_MASK) >> PIEN_SUDAVIE_LSB)
#define PIEN_SUDAVIE_SET(x)                      (((x) << PIEN_SUDAVIE_LSB) & PIEN_SUDAVIE_MASK)

#define FNCTRL_ADDRESS                           0x000001a4
#define FNCTRL_OFFSET                            0x000001a4
#define FNCTRL_CLKGATE_MSB                       31
#define FNCTRL_CLKGATE_LSB                       24
#define FNCTRL_CLKGATE_MASK                      0xff000000
#define FNCTRL_CLKGATE_GET(x)                    (((x) & FNCTRL_CLKGATE_MASK) >> FNCTRL_CLKGATE_LSB)
#define FNCTRL_CLKGATE_SET(x)                    (((x) << FNCTRL_CLKGATE_LSB) & FNCTRL_CLKGATE_MASK)
#define FNCTRL_FNADDR_MSB                        22
#define FNCTRL_FNADDR_LSB                        16
#define FNCTRL_FNADDR_MASK                       0x007f0000
#define FNCTRL_FNADDR_GET(x)                     (((x) & FNCTRL_FNADDR_MASK) >> FNCTRL_FNADDR_LSB)
#define FNCTRL_FNADDR_SET(x)                     (((x) << FNCTRL_FNADDR_LSB) & FNCTRL_FNADDR_MASK)
#define FNCTRL_FRMNR1_MSB                        13
#define FNCTRL_FRMNR1_LSB                        8
#define FNCTRL_FRMNR1_MASK                       0x00003f00
#define FNCTRL_FRMNR1_GET(x)                     (((x) & FNCTRL_FRMNR1_MASK) >> FNCTRL_FRMNR1_LSB)
#define FNCTRL_FRMNR1_SET(x)                     (((x) << FNCTRL_FRMNR1_LSB) & FNCTRL_FRMNR1_MASK)
#define FNCTRL_FRMNR0_MSB                        7
#define FNCTRL_FRMNR0_LSB                        3
#define FNCTRL_FRMNR0_MASK                       0x000000f8
#define FNCTRL_FRMNR0_GET(x)                     (((x) & FNCTRL_FRMNR0_MASK) >> FNCTRL_FRMNR0_LSB)
#define FNCTRL_FRMNR0_SET(x)                     (((x) << FNCTRL_FRMNR0_LSB) & FNCTRL_FRMNR0_MASK)
#define FNCTRL_MFR_MSB                           2
#define FNCTRL_MFR_LSB                           0
#define FNCTRL_MFR_MASK                          0x00000007
#define FNCTRL_MFR_GET(x)                        (((x) & FNCTRL_MFR_MASK) >> FNCTRL_MFR_LSB)
#define FNCTRL_MFR_SET(x)                        (((x) << FNCTRL_MFR_LSB) & FNCTRL_MFR_MASK)

#define OTGREG_ADDRESS                           0x000001bc
#define OTGREG_OFFSET                            0x000001bc
#define OTGREG_OTGSTATUS_ID_MSB                  30
#define OTGREG_OTGSTATUS_ID_LSB                  30
#define OTGREG_OTGSTATUS_ID_MASK                 0x40000000
#define OTGREG_OTGSTATUS_ID_GET(x)               (((x) & OTGREG_OTGSTATUS_ID_MASK) >> OTGREG_OTGSTATUS_ID_LSB)
#define OTGREG_OTGSTATUS_ID_SET(x)               (((x) << OTGREG_OTGSTATUS_ID_LSB) & OTGREG_OTGSTATUS_ID_MASK)
#define OTGREG_OTGSTATUS_AVBUSVAL_MSB            29
#define OTGREG_OTGSTATUS_AVBUSVAL_LSB            29
#define OTGREG_OTGSTATUS_AVBUSVAL_MASK           0x20000000
#define OTGREG_OTGSTATUS_AVBUSVAL_GET(x)         (((x) & OTGREG_OTGSTATUS_AVBUSVAL_MASK) >> OTGREG_OTGSTATUS_AVBUSVAL_LSB)
#define OTGREG_OTGSTATUS_AVBUSVAL_SET(x)         (((x) << OTGREG_OTGSTATUS_AVBUSVAL_LSB) & OTGREG_OTGSTATUS_AVBUSVAL_MASK)
#define OTGREG_OTGSTATUS_BSESSEND_MSB            28
#define OTGREG_OTGSTATUS_BSESSEND_LSB            28
#define OTGREG_OTGSTATUS_BSESSEND_MASK           0x10000000
#define OTGREG_OTGSTATUS_BSESSEND_GET(x)         (((x) & OTGREG_OTGSTATUS_BSESSEND_MASK) >> OTGREG_OTGSTATUS_BSESSEND_LSB)
#define OTGREG_OTGSTATUS_BSESSEND_SET(x)         (((x) << OTGREG_OTGSTATUS_BSESSEND_LSB) & OTGREG_OTGSTATUS_BSESSEND_MASK)
#define OTGREG_OTGSTATUS_ASESSVAL_MSB            27
#define OTGREG_OTGSTATUS_ASESSVAL_LSB            27
#define OTGREG_OTGSTATUS_ASESSVAL_MASK           0x08000000
#define OTGREG_OTGSTATUS_ASESSVAL_GET(x)         (((x) & OTGREG_OTGSTATUS_ASESSVAL_MASK) >> OTGREG_OTGSTATUS_ASESSVAL_LSB)
#define OTGREG_OTGSTATUS_ASESSVAL_SET(x)         (((x) << OTGREG_OTGSTATUS_ASESSVAL_LSB) & OTGREG_OTGSTATUS_ASESSVAL_MASK)
#define OTGREG_OTGSTATUS_CONN_MSB                25
#define OTGREG_OTGSTATUS_CONN_LSB                25
#define OTGREG_OTGSTATUS_CONN_MASK               0x02000000
#define OTGREG_OTGSTATUS_CONN_GET(x)             (((x) & OTGREG_OTGSTATUS_CONN_MASK) >> OTGREG_OTGSTATUS_CONN_LSB)
#define OTGREG_OTGSTATUS_CONN_SET(x)             (((x) << OTGREG_OTGSTATUS_CONN_LSB) & OTGREG_OTGSTATUS_CONN_MASK)
#define OTGREG_OTGSTATUS_BSE0SRP_MSB             24
#define OTGREG_OTGSTATUS_BSE0SRP_LSB             24
#define OTGREG_OTGSTATUS_BSE0SRP_MASK            0x01000000
#define OTGREG_OTGSTATUS_BSE0SRP_GET(x)          (((x) & OTGREG_OTGSTATUS_BSE0SRP_MASK) >> OTGREG_OTGSTATUS_BSE0SRP_LSB)
#define OTGREG_OTGSTATUS_BSE0SRP_SET(x)          (((x) << OTGREG_OTGSTATUS_BSE0SRP_LSB) & OTGREG_OTGSTATUS_BSE0SRP_MASK)
#define OTGREG_OTGCTRL_FORCEBCONN_MSB            23
#define OTGREG_OTGCTRL_FORCEBCONN_LSB            23
#define OTGREG_OTGCTRL_FORCEBCONN_MASK           0x00800000
#define OTGREG_OTGCTRL_FORCEBCONN_GET(x)         (((x) & OTGREG_OTGCTRL_FORCEBCONN_MASK) >> OTGREG_OTGCTRL_FORCEBCONN_LSB)
#define OTGREG_OTGCTRL_FORCEBCONN_SET(x)         (((x) << OTGREG_OTGCTRL_FORCEBCONN_LSB) & OTGREG_OTGCTRL_FORCEBCONN_MASK)
#define OTGREG_OTGCTRL_SRPDATDETEN_MSB           21
#define OTGREG_OTGCTRL_SRPDATDETEN_LSB           21
#define OTGREG_OTGCTRL_SRPDATDETEN_MASK          0x00200000
#define OTGREG_OTGCTRL_SRPDATDETEN_GET(x)        (((x) & OTGREG_OTGCTRL_SRPDATDETEN_MASK) >> OTGREG_OTGCTRL_SRPDATDETEN_LSB)
#define OTGREG_OTGCTRL_SRPDATDETEN_SET(x)        (((x) << OTGREG_OTGCTRL_SRPDATDETEN_LSB) & OTGREG_OTGCTRL_SRPDATDETEN_MASK)
#define OTGREG_OTGCTRL_SRPVBUSDETEN_MSB          20
#define OTGREG_OTGCTRL_SRPVBUSDETEN_LSB          20
#define OTGREG_OTGCTRL_SRPVBUSDETEN_MASK         0x00100000
#define OTGREG_OTGCTRL_SRPVBUSDETEN_GET(x)       (((x) & OTGREG_OTGCTRL_SRPVBUSDETEN_MASK) >> OTGREG_OTGCTRL_SRPVBUSDETEN_LSB)
#define OTGREG_OTGCTRL_SRPVBUSDETEN_SET(x)       (((x) << OTGREG_OTGCTRL_SRPVBUSDETEN_LSB) & OTGREG_OTGCTRL_SRPVBUSDETEN_MASK)
#define OTGREG_OTGCTRL_BHNPEN_MSB                19
#define OTGREG_OTGCTRL_BHNPEN_LSB                19
#define OTGREG_OTGCTRL_BHNPEN_MASK               0x00080000
#define OTGREG_OTGCTRL_BHNPEN_GET(x)             (((x) & OTGREG_OTGCTRL_BHNPEN_MASK) >> OTGREG_OTGCTRL_BHNPEN_LSB)
#define OTGREG_OTGCTRL_BHNPEN_SET(x)             (((x) << OTGREG_OTGCTRL_BHNPEN_LSB) & OTGREG_OTGCTRL_BHNPEN_MASK)
#define OTGREG_OTGCTRL_ASETBHNPEN_MSB            18
#define OTGREG_OTGCTRL_ASETBHNPEN_LSB            18
#define OTGREG_OTGCTRL_ASETBHNPEN_MASK           0x00040000
#define OTGREG_OTGCTRL_ASETBHNPEN_GET(x)         (((x) & OTGREG_OTGCTRL_ASETBHNPEN_MASK) >> OTGREG_OTGCTRL_ASETBHNPEN_LSB)
#define OTGREG_OTGCTRL_ASETBHNPEN_SET(x)         (((x) << OTGREG_OTGCTRL_ASETBHNPEN_LSB) & OTGREG_OTGCTRL_ASETBHNPEN_MASK)
#define OTGREG_OTGCTRL_ABUSDROP_MSB              17
#define OTGREG_OTGCTRL_ABUSDROP_LSB              17
#define OTGREG_OTGCTRL_ABUSDROP_MASK             0x00020000
#define OTGREG_OTGCTRL_ABUSDROP_GET(x)           (((x) & OTGREG_OTGCTRL_ABUSDROP_MASK) >> OTGREG_OTGCTRL_ABUSDROP_LSB)
#define OTGREG_OTGCTRL_ABUSDROP_SET(x)           (((x) << OTGREG_OTGCTRL_ABUSDROP_LSB) & OTGREG_OTGCTRL_ABUSDROP_MASK)
#define OTGREG_OTGCTRL_BUSREQ_MSB                16
#define OTGREG_OTGCTRL_BUSREQ_LSB                16
#define OTGREG_OTGCTRL_BUSREQ_MASK               0x00010000
#define OTGREG_OTGCTRL_BUSREQ_GET(x)             (((x) & OTGREG_OTGCTRL_BUSREQ_MASK) >> OTGREG_OTGCTRL_BUSREQ_LSB)
#define OTGREG_OTGCTRL_BUSREQ_SET(x)             (((x) << OTGREG_OTGCTRL_BUSREQ_LSB) & OTGREG_OTGCTRL_BUSREQ_MASK)
#define OTGREG_OTGSTATE_MSB                      11
#define OTGREG_OTGSTATE_LSB                      8
#define OTGREG_OTGSTATE_MASK                     0x00000f00
#define OTGREG_OTGSTATE_GET(x)                   (((x) & OTGREG_OTGSTATE_MASK) >> OTGREG_OTGSTATE_LSB)
#define OTGREG_OTGSTATE_SET(x)                   (((x) << OTGREG_OTGSTATE_LSB) & OTGREG_OTGSTATE_MASK)
#define OTGREG_OTGIRQ_PERIPHIRQ_MSB              4
#define OTGREG_OTGIRQ_PERIPHIRQ_LSB              4
#define OTGREG_OTGIRQ_PERIPHIRQ_MASK             0x00000010
#define OTGREG_OTGIRQ_PERIPHIRQ_GET(x)           (((x) & OTGREG_OTGIRQ_PERIPHIRQ_MASK) >> OTGREG_OTGIRQ_PERIPHIRQ_LSB)
#define OTGREG_OTGIRQ_PERIPHIRQ_SET(x)           (((x) << OTGREG_OTGIRQ_PERIPHIRQ_LSB) & OTGREG_OTGIRQ_PERIPHIRQ_MASK)
#define OTGREG_OTGIRQ_VBUSERRIRQ_MSB             3
#define OTGREG_OTGIRQ_VBUSERRIRQ_LSB             3
#define OTGREG_OTGIRQ_VBUSERRIRQ_MASK            0x00000008
#define OTGREG_OTGIRQ_VBUSERRIRQ_GET(x)          (((x) & OTGREG_OTGIRQ_VBUSERRIRQ_MASK) >> OTGREG_OTGIRQ_VBUSERRIRQ_LSB)
#define OTGREG_OTGIRQ_VBUSERRIRQ_SET(x)          (((x) << OTGREG_OTGIRQ_VBUSERRIRQ_LSB) & OTGREG_OTGIRQ_VBUSERRIRQ_MASK)
#define OTGREG_OTGIRQ_LOCSOFIRQ_MSB              2
#define OTGREG_OTGIRQ_LOCSOFIRQ_LSB              2
#define OTGREG_OTGIRQ_LOCSOFIRQ_MASK             0x00000004
#define OTGREG_OTGIRQ_LOCSOFIRQ_GET(x)           (((x) & OTGREG_OTGIRQ_LOCSOFIRQ_MASK) >> OTGREG_OTGIRQ_LOCSOFIRQ_LSB)
#define OTGREG_OTGIRQ_LOCSOFIRQ_SET(x)           (((x) << OTGREG_OTGIRQ_LOCSOFIRQ_LSB) & OTGREG_OTGIRQ_LOCSOFIRQ_MASK)
#define OTGREG_OTGIRQ_SRPDETIRQ_MSB              1
#define OTGREG_OTGIRQ_SRPDETIRQ_LSB              1
#define OTGREG_OTGIRQ_SRPDETIRQ_MASK             0x00000002
#define OTGREG_OTGIRQ_SRPDETIRQ_GET(x)           (((x) & OTGREG_OTGIRQ_SRPDETIRQ_MASK) >> OTGREG_OTGIRQ_SRPDETIRQ_LSB)
#define OTGREG_OTGIRQ_SRPDETIRQ_SET(x)           (((x) << OTGREG_OTGIRQ_SRPDETIRQ_LSB) & OTGREG_OTGIRQ_SRPDETIRQ_MASK)
#define OTGREG_OTGIRQ_IDLEIRQ_MSB                0
#define OTGREG_OTGIRQ_IDLEIRQ_LSB                0
#define OTGREG_OTGIRQ_IDLEIRQ_MASK               0x00000001
#define OTGREG_OTGIRQ_IDLEIRQ_GET(x)             (((x) & OTGREG_OTGIRQ_IDLEIRQ_MASK) >> OTGREG_OTGIRQ_IDLEIRQ_LSB)
#define OTGREG_OTGIRQ_IDLEIRQ_SET(x)             (((x) << OTGREG_OTGIRQ_IDLEIRQ_LSB) & OTGREG_OTGIRQ_IDLEIRQ_MASK)

#define DMASTART_ADDRESS                         0x000001cc
#define DMASTART_OFFSET                          0x000001cc
#define DMASTART_OUT15_MSB                       31
#define DMASTART_OUT15_LSB                       31
#define DMASTART_OUT15_MASK                      0x80000000
#define DMASTART_OUT15_GET(x)                    (((x) & DMASTART_OUT15_MASK) >> DMASTART_OUT15_LSB)
#define DMASTART_OUT15_SET(x)                    (((x) << DMASTART_OUT15_LSB) & DMASTART_OUT15_MASK)
#define DMASTART_OUT14_MSB                       30
#define DMASTART_OUT14_LSB                       30
#define DMASTART_OUT14_MASK                      0x40000000
#define DMASTART_OUT14_GET(x)                    (((x) & DMASTART_OUT14_MASK) >> DMASTART_OUT14_LSB)
#define DMASTART_OUT14_SET(x)                    (((x) << DMASTART_OUT14_LSB) & DMASTART_OUT14_MASK)
#define DMASTART_OUT13_MSB                       29
#define DMASTART_OUT13_LSB                       29
#define DMASTART_OUT13_MASK                      0x20000000
#define DMASTART_OUT13_GET(x)                    (((x) & DMASTART_OUT13_MASK) >> DMASTART_OUT13_LSB)
#define DMASTART_OUT13_SET(x)                    (((x) << DMASTART_OUT13_LSB) & DMASTART_OUT13_MASK)
#define DMASTART_OUT12_MSB                       28
#define DMASTART_OUT12_LSB                       28
#define DMASTART_OUT12_MASK                      0x10000000
#define DMASTART_OUT12_GET(x)                    (((x) & DMASTART_OUT12_MASK) >> DMASTART_OUT12_LSB)
#define DMASTART_OUT12_SET(x)                    (((x) << DMASTART_OUT12_LSB) & DMASTART_OUT12_MASK)
#define DMASTART_OUT11_MSB                       27
#define DMASTART_OUT11_LSB                       27
#define DMASTART_OUT11_MASK                      0x08000000
#define DMASTART_OUT11_GET(x)                    (((x) & DMASTART_OUT11_MASK) >> DMASTART_OUT11_LSB)
#define DMASTART_OUT11_SET(x)                    (((x) << DMASTART_OUT11_LSB) & DMASTART_OUT11_MASK)
#define DMASTART_OUT10_MSB                       26
#define DMASTART_OUT10_LSB                       26
#define DMASTART_OUT10_MASK                      0x04000000
#define DMASTART_OUT10_GET(x)                    (((x) & DMASTART_OUT10_MASK) >> DMASTART_OUT10_LSB)
#define DMASTART_OUT10_SET(x)                    (((x) << DMASTART_OUT10_LSB) & DMASTART_OUT10_MASK)
#define DMASTART_OUT9_MSB                        25
#define DMASTART_OUT9_LSB                        25
#define DMASTART_OUT9_MASK                       0x02000000
#define DMASTART_OUT9_GET(x)                     (((x) & DMASTART_OUT9_MASK) >> DMASTART_OUT9_LSB)
#define DMASTART_OUT9_SET(x)                     (((x) << DMASTART_OUT9_LSB) & DMASTART_OUT9_MASK)
#define DMASTART_OUT8_MSB                        24
#define DMASTART_OUT8_LSB                        24
#define DMASTART_OUT8_MASK                       0x01000000
#define DMASTART_OUT8_GET(x)                     (((x) & DMASTART_OUT8_MASK) >> DMASTART_OUT8_LSB)
#define DMASTART_OUT8_SET(x)                     (((x) << DMASTART_OUT8_LSB) & DMASTART_OUT8_MASK)
#define DMASTART_OUT7_MSB                        23
#define DMASTART_OUT7_LSB                        23
#define DMASTART_OUT7_MASK                       0x00800000
#define DMASTART_OUT7_GET(x)                     (((x) & DMASTART_OUT7_MASK) >> DMASTART_OUT7_LSB)
#define DMASTART_OUT7_SET(x)                     (((x) << DMASTART_OUT7_LSB) & DMASTART_OUT7_MASK)
#define DMASTART_OUT6_MSB                        22
#define DMASTART_OUT6_LSB                        22
#define DMASTART_OUT6_MASK                       0x00400000
#define DMASTART_OUT6_GET(x)                     (((x) & DMASTART_OUT6_MASK) >> DMASTART_OUT6_LSB)
#define DMASTART_OUT6_SET(x)                     (((x) << DMASTART_OUT6_LSB) & DMASTART_OUT6_MASK)
#define DMASTART_OUT5_MSB                        21
#define DMASTART_OUT5_LSB                        21
#define DMASTART_OUT5_MASK                       0x00200000
#define DMASTART_OUT5_GET(x)                     (((x) & DMASTART_OUT5_MASK) >> DMASTART_OUT5_LSB)
#define DMASTART_OUT5_SET(x)                     (((x) << DMASTART_OUT5_LSB) & DMASTART_OUT5_MASK)
#define DMASTART_OUT4_MSB                        20
#define DMASTART_OUT4_LSB                        20
#define DMASTART_OUT4_MASK                       0x00100000
#define DMASTART_OUT4_GET(x)                     (((x) & DMASTART_OUT4_MASK) >> DMASTART_OUT4_LSB)
#define DMASTART_OUT4_SET(x)                     (((x) << DMASTART_OUT4_LSB) & DMASTART_OUT4_MASK)
#define DMASTART_OUT3_MSB                        19
#define DMASTART_OUT3_LSB                        19
#define DMASTART_OUT3_MASK                       0x00080000
#define DMASTART_OUT3_GET(x)                     (((x) & DMASTART_OUT3_MASK) >> DMASTART_OUT3_LSB)
#define DMASTART_OUT3_SET(x)                     (((x) << DMASTART_OUT3_LSB) & DMASTART_OUT3_MASK)
#define DMASTART_OUT2_MSB                        18
#define DMASTART_OUT2_LSB                        18
#define DMASTART_OUT2_MASK                       0x00040000
#define DMASTART_OUT2_GET(x)                     (((x) & DMASTART_OUT2_MASK) >> DMASTART_OUT2_LSB)
#define DMASTART_OUT2_SET(x)                     (((x) << DMASTART_OUT2_LSB) & DMASTART_OUT2_MASK)
#define DMASTART_OUT1_MSB                        17
#define DMASTART_OUT1_LSB                        17
#define DMASTART_OUT1_MASK                       0x00020000
#define DMASTART_OUT1_GET(x)                     (((x) & DMASTART_OUT1_MASK) >> DMASTART_OUT1_LSB)
#define DMASTART_OUT1_SET(x)                     (((x) << DMASTART_OUT1_LSB) & DMASTART_OUT1_MASK)
#define DMASTART_OUT0_MSB                        16
#define DMASTART_OUT0_LSB                        16
#define DMASTART_OUT0_MASK                       0x00010000
#define DMASTART_OUT0_GET(x)                     (((x) & DMASTART_OUT0_MASK) >> DMASTART_OUT0_LSB)
#define DMASTART_OUT0_SET(x)                     (((x) << DMASTART_OUT0_LSB) & DMASTART_OUT0_MASK)
#define DMASTART_IN15_MSB                        15
#define DMASTART_IN15_LSB                        15
#define DMASTART_IN15_MASK                       0x00008000
#define DMASTART_IN15_GET(x)                     (((x) & DMASTART_IN15_MASK) >> DMASTART_IN15_LSB)
#define DMASTART_IN15_SET(x)                     (((x) << DMASTART_IN15_LSB) & DMASTART_IN15_MASK)
#define DMASTART_IN14_MSB                        14
#define DMASTART_IN14_LSB                        14
#define DMASTART_IN14_MASK                       0x00004000
#define DMASTART_IN14_GET(x)                     (((x) & DMASTART_IN14_MASK) >> DMASTART_IN14_LSB)
#define DMASTART_IN14_SET(x)                     (((x) << DMASTART_IN14_LSB) & DMASTART_IN14_MASK)
#define DMASTART_IN13_MSB                        13
#define DMASTART_IN13_LSB                        13
#define DMASTART_IN13_MASK                       0x00002000
#define DMASTART_IN13_GET(x)                     (((x) & DMASTART_IN13_MASK) >> DMASTART_IN13_LSB)
#define DMASTART_IN13_SET(x)                     (((x) << DMASTART_IN13_LSB) & DMASTART_IN13_MASK)
#define DMASTART_IN12_MSB                        12
#define DMASTART_IN12_LSB                        12
#define DMASTART_IN12_MASK                       0x00001000
#define DMASTART_IN12_GET(x)                     (((x) & DMASTART_IN12_MASK) >> DMASTART_IN12_LSB)
#define DMASTART_IN12_SET(x)                     (((x) << DMASTART_IN12_LSB) & DMASTART_IN12_MASK)
#define DMASTART_IN11_MSB                        11
#define DMASTART_IN11_LSB                        11
#define DMASTART_IN11_MASK                       0x00000800
#define DMASTART_IN11_GET(x)                     (((x) & DMASTART_IN11_MASK) >> DMASTART_IN11_LSB)
#define DMASTART_IN11_SET(x)                     (((x) << DMASTART_IN11_LSB) & DMASTART_IN11_MASK)
#define DMASTART_IN10_MSB                        10
#define DMASTART_IN10_LSB                        10
#define DMASTART_IN10_MASK                       0x00000400
#define DMASTART_IN10_GET(x)                     (((x) & DMASTART_IN10_MASK) >> DMASTART_IN10_LSB)
#define DMASTART_IN10_SET(x)                     (((x) << DMASTART_IN10_LSB) & DMASTART_IN10_MASK)
#define DMASTART_IN9_MSB                         9
#define DMASTART_IN9_LSB                         9
#define DMASTART_IN9_MASK                        0x00000200
#define DMASTART_IN9_GET(x)                      (((x) & DMASTART_IN9_MASK) >> DMASTART_IN9_LSB)
#define DMASTART_IN9_SET(x)                      (((x) << DMASTART_IN9_LSB) & DMASTART_IN9_MASK)
#define DMASTART_IN8_MSB                         8
#define DMASTART_IN8_LSB                         8
#define DMASTART_IN8_MASK                        0x00000100
#define DMASTART_IN8_GET(x)                      (((x) & DMASTART_IN8_MASK) >> DMASTART_IN8_LSB)
#define DMASTART_IN8_SET(x)                      (((x) << DMASTART_IN8_LSB) & DMASTART_IN8_MASK)
#define DMASTART_IN7_MSB                         7
#define DMASTART_IN7_LSB                         7
#define DMASTART_IN7_MASK                        0x00000080
#define DMASTART_IN7_GET(x)                      (((x) & DMASTART_IN7_MASK) >> DMASTART_IN7_LSB)
#define DMASTART_IN7_SET(x)                      (((x) << DMASTART_IN7_LSB) & DMASTART_IN7_MASK)
#define DMASTART_IN6_MSB                         6
#define DMASTART_IN6_LSB                         6
#define DMASTART_IN6_MASK                        0x00000040
#define DMASTART_IN6_GET(x)                      (((x) & DMASTART_IN6_MASK) >> DMASTART_IN6_LSB)
#define DMASTART_IN6_SET(x)                      (((x) << DMASTART_IN6_LSB) & DMASTART_IN6_MASK)
#define DMASTART_IN5_MSB                         5
#define DMASTART_IN5_LSB                         5
#define DMASTART_IN5_MASK                        0x00000020
#define DMASTART_IN5_GET(x)                      (((x) & DMASTART_IN5_MASK) >> DMASTART_IN5_LSB)
#define DMASTART_IN5_SET(x)                      (((x) << DMASTART_IN5_LSB) & DMASTART_IN5_MASK)
#define DMASTART_IN4_MSB                         4
#define DMASTART_IN4_LSB                         4
#define DMASTART_IN4_MASK                        0x00000010
#define DMASTART_IN4_GET(x)                      (((x) & DMASTART_IN4_MASK) >> DMASTART_IN4_LSB)
#define DMASTART_IN4_SET(x)                      (((x) << DMASTART_IN4_LSB) & DMASTART_IN4_MASK)
#define DMASTART_IN3_MSB                         3
#define DMASTART_IN3_LSB                         3
#define DMASTART_IN3_MASK                        0x00000008
#define DMASTART_IN3_GET(x)                      (((x) & DMASTART_IN3_MASK) >> DMASTART_IN3_LSB)
#define DMASTART_IN3_SET(x)                      (((x) << DMASTART_IN3_LSB) & DMASTART_IN3_MASK)
#define DMASTART_IN2_MSB                         2
#define DMASTART_IN2_LSB                         2
#define DMASTART_IN2_MASK                        0x00000004
#define DMASTART_IN2_GET(x)                      (((x) & DMASTART_IN2_MASK) >> DMASTART_IN2_LSB)
#define DMASTART_IN2_SET(x)                      (((x) << DMASTART_IN2_LSB) & DMASTART_IN2_MASK)
#define DMASTART_IN1_MSB                         1
#define DMASTART_IN1_LSB                         1
#define DMASTART_IN1_MASK                        0x00000002
#define DMASTART_IN1_GET(x)                      (((x) & DMASTART_IN1_MASK) >> DMASTART_IN1_LSB)
#define DMASTART_IN1_SET(x)                      (((x) << DMASTART_IN1_LSB) & DMASTART_IN1_MASK)
#define DMASTART_IN0_MSB                         0
#define DMASTART_IN0_LSB                         0
#define DMASTART_IN0_MASK                        0x00000001
#define DMASTART_IN0_GET(x)                      (((x) & DMASTART_IN0_MASK) >> DMASTART_IN0_LSB)
#define DMASTART_IN0_SET(x)                      (((x) << DMASTART_IN0_LSB) & DMASTART_IN0_MASK)

#define DMASTOP_ADDRESS                          0x000001d0
#define DMASTOP_OFFSET                           0x000001d0
#define DMASTOP_OUT15_MSB                        31
#define DMASTOP_OUT15_LSB                        31
#define DMASTOP_OUT15_MASK                       0x80000000
#define DMASTOP_OUT15_GET(x)                     (((x) & DMASTOP_OUT15_MASK) >> DMASTOP_OUT15_LSB)
#define DMASTOP_OUT15_SET(x)                     (((x) << DMASTOP_OUT15_LSB) & DMASTOP_OUT15_MASK)
#define DMASTOP_OUT14_MSB                        30
#define DMASTOP_OUT14_LSB                        30
#define DMASTOP_OUT14_MASK                       0x40000000
#define DMASTOP_OUT14_GET(x)                     (((x) & DMASTOP_OUT14_MASK) >> DMASTOP_OUT14_LSB)
#define DMASTOP_OUT14_SET(x)                     (((x) << DMASTOP_OUT14_LSB) & DMASTOP_OUT14_MASK)
#define DMASTOP_OUT13_MSB                        29
#define DMASTOP_OUT13_LSB                        29
#define DMASTOP_OUT13_MASK                       0x20000000
#define DMASTOP_OUT13_GET(x)                     (((x) & DMASTOP_OUT13_MASK) >> DMASTOP_OUT13_LSB)
#define DMASTOP_OUT13_SET(x)                     (((x) << DMASTOP_OUT13_LSB) & DMASTOP_OUT13_MASK)
#define DMASTOP_OUT12_MSB                        28
#define DMASTOP_OUT12_LSB                        28
#define DMASTOP_OUT12_MASK                       0x10000000
#define DMASTOP_OUT12_GET(x)                     (((x) & DMASTOP_OUT12_MASK) >> DMASTOP_OUT12_LSB)
#define DMASTOP_OUT12_SET(x)                     (((x) << DMASTOP_OUT12_LSB) & DMASTOP_OUT12_MASK)
#define DMASTOP_OUT11_MSB                        27
#define DMASTOP_OUT11_LSB                        27
#define DMASTOP_OUT11_MASK                       0x08000000
#define DMASTOP_OUT11_GET(x)                     (((x) & DMASTOP_OUT11_MASK) >> DMASTOP_OUT11_LSB)
#define DMASTOP_OUT11_SET(x)                     (((x) << DMASTOP_OUT11_LSB) & DMASTOP_OUT11_MASK)
#define DMASTOP_OUT10_MSB                        26
#define DMASTOP_OUT10_LSB                        26
#define DMASTOP_OUT10_MASK                       0x04000000
#define DMASTOP_OUT10_GET(x)                     (((x) & DMASTOP_OUT10_MASK) >> DMASTOP_OUT10_LSB)
#define DMASTOP_OUT10_SET(x)                     (((x) << DMASTOP_OUT10_LSB) & DMASTOP_OUT10_MASK)
#define DMASTOP_OUT9_MSB                         25
#define DMASTOP_OUT9_LSB                         25
#define DMASTOP_OUT9_MASK                        0x02000000
#define DMASTOP_OUT9_GET(x)                      (((x) & DMASTOP_OUT9_MASK) >> DMASTOP_OUT9_LSB)
#define DMASTOP_OUT9_SET(x)                      (((x) << DMASTOP_OUT9_LSB) & DMASTOP_OUT9_MASK)
#define DMASTOP_OUT8_MSB                         24
#define DMASTOP_OUT8_LSB                         24
#define DMASTOP_OUT8_MASK                        0x01000000
#define DMASTOP_OUT8_GET(x)                      (((x) & DMASTOP_OUT8_MASK) >> DMASTOP_OUT8_LSB)
#define DMASTOP_OUT8_SET(x)                      (((x) << DMASTOP_OUT8_LSB) & DMASTOP_OUT8_MASK)
#define DMASTOP_OUT7_MSB                         23
#define DMASTOP_OUT7_LSB                         23
#define DMASTOP_OUT7_MASK                        0x00800000
#define DMASTOP_OUT7_GET(x)                      (((x) & DMASTOP_OUT7_MASK) >> DMASTOP_OUT7_LSB)
#define DMASTOP_OUT7_SET(x)                      (((x) << DMASTOP_OUT7_LSB) & DMASTOP_OUT7_MASK)
#define DMASTOP_OUT6_MSB                         22
#define DMASTOP_OUT6_LSB                         22
#define DMASTOP_OUT6_MASK                        0x00400000
#define DMASTOP_OUT6_GET(x)                      (((x) & DMASTOP_OUT6_MASK) >> DMASTOP_OUT6_LSB)
#define DMASTOP_OUT6_SET(x)                      (((x) << DMASTOP_OUT6_LSB) & DMASTOP_OUT6_MASK)
#define DMASTOP_OUT5_MSB                         21
#define DMASTOP_OUT5_LSB                         21
#define DMASTOP_OUT5_MASK                        0x00200000
#define DMASTOP_OUT5_GET(x)                      (((x) & DMASTOP_OUT5_MASK) >> DMASTOP_OUT5_LSB)
#define DMASTOP_OUT5_SET(x)                      (((x) << DMASTOP_OUT5_LSB) & DMASTOP_OUT5_MASK)
#define DMASTOP_OUT4_MSB                         20
#define DMASTOP_OUT4_LSB                         20
#define DMASTOP_OUT4_MASK                        0x00100000
#define DMASTOP_OUT4_GET(x)                      (((x) & DMASTOP_OUT4_MASK) >> DMASTOP_OUT4_LSB)
#define DMASTOP_OUT4_SET(x)                      (((x) << DMASTOP_OUT4_LSB) & DMASTOP_OUT4_MASK)
#define DMASTOP_OUT3_MSB                         19
#define DMASTOP_OUT3_LSB                         19
#define DMASTOP_OUT3_MASK                        0x00080000
#define DMASTOP_OUT3_GET(x)                      (((x) & DMASTOP_OUT3_MASK) >> DMASTOP_OUT3_LSB)
#define DMASTOP_OUT3_SET(x)                      (((x) << DMASTOP_OUT3_LSB) & DMASTOP_OUT3_MASK)
#define DMASTOP_OUT2_MSB                         18
#define DMASTOP_OUT2_LSB                         18
#define DMASTOP_OUT2_MASK                        0x00040000
#define DMASTOP_OUT2_GET(x)                      (((x) & DMASTOP_OUT2_MASK) >> DMASTOP_OUT2_LSB)
#define DMASTOP_OUT2_SET(x)                      (((x) << DMASTOP_OUT2_LSB) & DMASTOP_OUT2_MASK)
#define DMASTOP_OUT1_MSB                         17
#define DMASTOP_OUT1_LSB                         17
#define DMASTOP_OUT1_MASK                        0x00020000
#define DMASTOP_OUT1_GET(x)                      (((x) & DMASTOP_OUT1_MASK) >> DMASTOP_OUT1_LSB)
#define DMASTOP_OUT1_SET(x)                      (((x) << DMASTOP_OUT1_LSB) & DMASTOP_OUT1_MASK)
#define DMASTOP_OUT0_MSB                         16
#define DMASTOP_OUT0_LSB                         16
#define DMASTOP_OUT0_MASK                        0x00010000
#define DMASTOP_OUT0_GET(x)                      (((x) & DMASTOP_OUT0_MASK) >> DMASTOP_OUT0_LSB)
#define DMASTOP_OUT0_SET(x)                      (((x) << DMASTOP_OUT0_LSB) & DMASTOP_OUT0_MASK)
#define DMASTOP_IN15_MSB                         15
#define DMASTOP_IN15_LSB                         15
#define DMASTOP_IN15_MASK                        0x00008000
#define DMASTOP_IN15_GET(x)                      (((x) & DMASTOP_IN15_MASK) >> DMASTOP_IN15_LSB)
#define DMASTOP_IN15_SET(x)                      (((x) << DMASTOP_IN15_LSB) & DMASTOP_IN15_MASK)
#define DMASTOP_IN14_MSB                         14
#define DMASTOP_IN14_LSB                         14
#define DMASTOP_IN14_MASK                        0x00004000
#define DMASTOP_IN14_GET(x)                      (((x) & DMASTOP_IN14_MASK) >> DMASTOP_IN14_LSB)
#define DMASTOP_IN14_SET(x)                      (((x) << DMASTOP_IN14_LSB) & DMASTOP_IN14_MASK)
#define DMASTOP_IN13_MSB                         13
#define DMASTOP_IN13_LSB                         13
#define DMASTOP_IN13_MASK                        0x00002000
#define DMASTOP_IN13_GET(x)                      (((x) & DMASTOP_IN13_MASK) >> DMASTOP_IN13_LSB)
#define DMASTOP_IN13_SET(x)                      (((x) << DMASTOP_IN13_LSB) & DMASTOP_IN13_MASK)
#define DMASTOP_IN12_MSB                         12
#define DMASTOP_IN12_LSB                         12
#define DMASTOP_IN12_MASK                        0x00001000
#define DMASTOP_IN12_GET(x)                      (((x) & DMASTOP_IN12_MASK) >> DMASTOP_IN12_LSB)
#define DMASTOP_IN12_SET(x)                      (((x) << DMASTOP_IN12_LSB) & DMASTOP_IN12_MASK)
#define DMASTOP_IN11_MSB                         11
#define DMASTOP_IN11_LSB                         11
#define DMASTOP_IN11_MASK                        0x00000800
#define DMASTOP_IN11_GET(x)                      (((x) & DMASTOP_IN11_MASK) >> DMASTOP_IN11_LSB)
#define DMASTOP_IN11_SET(x)                      (((x) << DMASTOP_IN11_LSB) & DMASTOP_IN11_MASK)
#define DMASTOP_IN10_MSB                         10
#define DMASTOP_IN10_LSB                         10
#define DMASTOP_IN10_MASK                        0x00000400
#define DMASTOP_IN10_GET(x)                      (((x) & DMASTOP_IN10_MASK) >> DMASTOP_IN10_LSB)
#define DMASTOP_IN10_SET(x)                      (((x) << DMASTOP_IN10_LSB) & DMASTOP_IN10_MASK)
#define DMASTOP_IN9_MSB                          9
#define DMASTOP_IN9_LSB                          9
#define DMASTOP_IN9_MASK                         0x00000200
#define DMASTOP_IN9_GET(x)                       (((x) & DMASTOP_IN9_MASK) >> DMASTOP_IN9_LSB)
#define DMASTOP_IN9_SET(x)                       (((x) << DMASTOP_IN9_LSB) & DMASTOP_IN9_MASK)
#define DMASTOP_IN8_MSB                          8
#define DMASTOP_IN8_LSB                          8
#define DMASTOP_IN8_MASK                         0x00000100
#define DMASTOP_IN8_GET(x)                       (((x) & DMASTOP_IN8_MASK) >> DMASTOP_IN8_LSB)
#define DMASTOP_IN8_SET(x)                       (((x) << DMASTOP_IN8_LSB) & DMASTOP_IN8_MASK)
#define DMASTOP_IN7_MSB                          7
#define DMASTOP_IN7_LSB                          7
#define DMASTOP_IN7_MASK                         0x00000080
#define DMASTOP_IN7_GET(x)                       (((x) & DMASTOP_IN7_MASK) >> DMASTOP_IN7_LSB)
#define DMASTOP_IN7_SET(x)                       (((x) << DMASTOP_IN7_LSB) & DMASTOP_IN7_MASK)
#define DMASTOP_IN6_MSB                          6
#define DMASTOP_IN6_LSB                          6
#define DMASTOP_IN6_MASK                         0x00000040
#define DMASTOP_IN6_GET(x)                       (((x) & DMASTOP_IN6_MASK) >> DMASTOP_IN6_LSB)
#define DMASTOP_IN6_SET(x)                       (((x) << DMASTOP_IN6_LSB) & DMASTOP_IN6_MASK)
#define DMASTOP_IN5_MSB                          5
#define DMASTOP_IN5_LSB                          5
#define DMASTOP_IN5_MASK                         0x00000020
#define DMASTOP_IN5_GET(x)                       (((x) & DMASTOP_IN5_MASK) >> DMASTOP_IN5_LSB)
#define DMASTOP_IN5_SET(x)                       (((x) << DMASTOP_IN5_LSB) & DMASTOP_IN5_MASK)
#define DMASTOP_IN4_MSB                          4
#define DMASTOP_IN4_LSB                          4
#define DMASTOP_IN4_MASK                         0x00000010
#define DMASTOP_IN4_GET(x)                       (((x) & DMASTOP_IN4_MASK) >> DMASTOP_IN4_LSB)
#define DMASTOP_IN4_SET(x)                       (((x) << DMASTOP_IN4_LSB) & DMASTOP_IN4_MASK)
#define DMASTOP_IN3_MSB                          3
#define DMASTOP_IN3_LSB                          3
#define DMASTOP_IN3_MASK                         0x00000008
#define DMASTOP_IN3_GET(x)                       (((x) & DMASTOP_IN3_MASK) >> DMASTOP_IN3_LSB)
#define DMASTOP_IN3_SET(x)                       (((x) << DMASTOP_IN3_LSB) & DMASTOP_IN3_MASK)
#define DMASTOP_IN2_MSB                          2
#define DMASTOP_IN2_LSB                          2
#define DMASTOP_IN2_MASK                         0x00000004
#define DMASTOP_IN2_GET(x)                       (((x) & DMASTOP_IN2_MASK) >> DMASTOP_IN2_LSB)
#define DMASTOP_IN2_SET(x)                       (((x) << DMASTOP_IN2_LSB) & DMASTOP_IN2_MASK)
#define DMASTOP_IN1_MSB                          1
#define DMASTOP_IN1_LSB                          1
#define DMASTOP_IN1_MASK                         0x00000002
#define DMASTOP_IN1_GET(x)                       (((x) & DMASTOP_IN1_MASK) >> DMASTOP_IN1_LSB)
#define DMASTOP_IN1_SET(x)                       (((x) << DMASTOP_IN1_LSB) & DMASTOP_IN1_MASK)
#define DMASTOP_IN0_MSB                          0
#define DMASTOP_IN0_LSB                          0
#define DMASTOP_IN0_MASK                         0x00000001
#define DMASTOP_IN0_GET(x)                       (((x) & DMASTOP_IN0_MASK) >> DMASTOP_IN0_LSB)
#define DMASTOP_IN0_SET(x)                       (((x) << DMASTOP_IN0_LSB) & DMASTOP_IN0_MASK)

#define EP0DMAADDR_ADDRESS                       0x00000400
#define EP0DMAADDR_OFFSET                        0x00000400
#define EP0DMAADDR_ADDR_MSB                      31
#define EP0DMAADDR_ADDR_LSB                      2
#define EP0DMAADDR_ADDR_MASK                     0xfffffffc
#define EP0DMAADDR_ADDR_GET(x)                   (((x) & EP0DMAADDR_ADDR_MASK) >> EP0DMAADDR_ADDR_LSB)
#define EP0DMAADDR_ADDR_SET(x)                   (((x) << EP0DMAADDR_ADDR_LSB) & EP0DMAADDR_ADDR_MASK)

#define EP1DMAADDR_ADDRESS                       0x00000420
#define EP1DMAADDR_OFFSET                        0x00000420
#define EP1DMAADDR_ADDR_MSB                      31
#define EP1DMAADDR_ADDR_LSB                      2
#define EP1DMAADDR_ADDR_MASK                     0xfffffffc
#define EP1DMAADDR_ADDR_GET(x)                   (((x) & EP1DMAADDR_ADDR_MASK) >> EP1DMAADDR_ADDR_LSB)
#define EP1DMAADDR_ADDR_SET(x)                   (((x) << EP1DMAADDR_ADDR_LSB) & EP1DMAADDR_ADDR_MASK)

#define OUT1DMACTRL_ADDRESS                      0x0000042c
#define OUT1DMACTRL_OFFSET                       0x0000042c
#define OUT1DMACTRL_HRPROT_MSB                   31
#define OUT1DMACTRL_HRPROT_LSB                   28
#define OUT1DMACTRL_HRPROT_MASK                  0xf0000000
#define OUT1DMACTRL_HRPROT_GET(x)                (((x) & OUT1DMACTRL_HRPROT_MASK) >> OUT1DMACTRL_HRPROT_LSB)
#define OUT1DMACTRL_HRPROT_SET(x)                (((x) << OUT1DMACTRL_HRPROT_LSB) & OUT1DMACTRL_HRPROT_MASK)
#define OUT1DMACTRL_HSIZE_MSB                    27
#define OUT1DMACTRL_HSIZE_LSB                    26
#define OUT1DMACTRL_HSIZE_MASK                   0x0c000000
#define OUT1DMACTRL_HSIZE_GET(x)                 (((x) & OUT1DMACTRL_HSIZE_MASK) >> OUT1DMACTRL_HSIZE_LSB)
#define OUT1DMACTRL_HSIZE_SET(x)                 (((x) << OUT1DMACTRL_HSIZE_LSB) & OUT1DMACTRL_HSIZE_MASK)
#define OUT1DMACTRL_HLOCK_MSB                    25
#define OUT1DMACTRL_HLOCK_LSB                    25
#define OUT1DMACTRL_HLOCK_MASK                   0x02000000
#define OUT1DMACTRL_HLOCK_GET(x)                 (((x) & OUT1DMACTRL_HLOCK_MASK) >> OUT1DMACTRL_HLOCK_LSB)
#define OUT1DMACTRL_HLOCK_SET(x)                 (((x) << OUT1DMACTRL_HLOCK_LSB) & OUT1DMACTRL_HLOCK_MASK)
#define OUT1DMACTRL_DMARING_MSB                  22
#define OUT1DMACTRL_DMARING_LSB                  22
#define OUT1DMACTRL_DMARING_MASK                 0x00400000
#define OUT1DMACTRL_DMARING_GET(x)               (((x) & OUT1DMACTRL_DMARING_MASK) >> OUT1DMACTRL_DMARING_LSB)
#define OUT1DMACTRL_DMARING_SET(x)               (((x) << OUT1DMACTRL_DMARING_LSB) & OUT1DMACTRL_DMARING_MASK)
#define OUT1DMACTRL_DMANINCR_MSB                 21
#define OUT1DMACTRL_DMANINCR_LSB                 21
#define OUT1DMACTRL_DMANINCR_MASK                0x00200000
#define OUT1DMACTRL_DMANINCR_GET(x)              (((x) & OUT1DMACTRL_DMANINCR_MASK) >> OUT1DMACTRL_DMANINCR_LSB)
#define OUT1DMACTRL_DMANINCR_SET(x)              (((x) << OUT1DMACTRL_DMANINCR_LSB) & OUT1DMACTRL_DMANINCR_MASK)
#define OUT1DMACTRL_DMATUNLIM_MSB                20
#define OUT1DMACTRL_DMATUNLIM_LSB                20
#define OUT1DMACTRL_DMATUNLIM_MASK               0x00100000
#define OUT1DMACTRL_DMATUNLIM_GET(x)             (((x) & OUT1DMACTRL_DMATUNLIM_MASK) >> OUT1DMACTRL_DMATUNLIM_LSB)
#define OUT1DMACTRL_DMATUNLIM_SET(x)             (((x) << OUT1DMACTRL_DMATUNLIM_LSB) & OUT1DMACTRL_DMATUNLIM_MASK)
#define OUT1DMACTRL_DMASTART_MSB                 18
#define OUT1DMACTRL_DMASTART_LSB                 18
#define OUT1DMACTRL_DMASTART_MASK                0x00040000
#define OUT1DMACTRL_DMASTART_GET(x)              (((x) & OUT1DMACTRL_DMASTART_MASK) >> OUT1DMACTRL_DMASTART_LSB)
#define OUT1DMACTRL_DMASTART_SET(x)              (((x) << OUT1DMACTRL_DMASTART_LSB) & OUT1DMACTRL_DMASTART_MASK)
#define OUT1DMACTRL_DMASTOP_MSB                  17
#define OUT1DMACTRL_DMASTOP_LSB                  17
#define OUT1DMACTRL_DMASTOP_MASK                 0x00020000
#define OUT1DMACTRL_DMASTOP_GET(x)               (((x) & OUT1DMACTRL_DMASTOP_MASK) >> OUT1DMACTRL_DMASTOP_LSB)
#define OUT1DMACTRL_DMASTOP_SET(x)               (((x) << OUT1DMACTRL_DMASTOP_LSB) & OUT1DMACTRL_DMASTOP_MASK)
#define OUT1DMACTRL_ENDIAN_MSB                   16
#define OUT1DMACTRL_ENDIAN_LSB                   16
#define OUT1DMACTRL_ENDIAN_MASK                  0x00010000
#define OUT1DMACTRL_ENDIAN_GET(x)                (((x) & OUT1DMACTRL_ENDIAN_MASK) >> OUT1DMACTRL_ENDIAN_LSB)
#define OUT1DMACTRL_ENDIAN_SET(x)                (((x) << OUT1DMACTRL_ENDIAN_LSB) & OUT1DMACTRL_ENDIAN_MASK)
#define OUT1DMACTRL_RINGSIZ_MSB                  15
#define OUT1DMACTRL_RINGSIZ_LSB                  2
#define OUT1DMACTRL_RINGSIZ_MASK                 0x0000fffc
#define OUT1DMACTRL_RINGSIZ_GET(x)               (((x) & OUT1DMACTRL_RINGSIZ_MASK) >> OUT1DMACTRL_RINGSIZ_LSB)
#define OUT1DMACTRL_RINGSIZ_SET(x)               (((x) << OUT1DMACTRL_RINGSIZ_LSB) & OUT1DMACTRL_RINGSIZ_MASK)

#define EP2DMAADDR_ADDRESS                       0x00000440
#define EP2DMAADDR_OFFSET                        0x00000440
#define EP2DMAADDR_ADDR_MSB                      31
#define EP2DMAADDR_ADDR_LSB                      2
#define EP2DMAADDR_ADDR_MASK                     0xfffffffc
#define EP2DMAADDR_ADDR_GET(x)                   (((x) & EP2DMAADDR_ADDR_MASK) >> EP2DMAADDR_ADDR_LSB)
#define EP2DMAADDR_ADDR_SET(x)                   (((x) << EP2DMAADDR_ADDR_LSB) & EP2DMAADDR_ADDR_MASK)

#define OUT2DMACTRL_ADDRESS                      0x0000044c
#define OUT2DMACTRL_OFFSET                       0x0000044c
#define OUT2DMACTRL_HRPROT_MSB                   31
#define OUT2DMACTRL_HRPROT_LSB                   28
#define OUT2DMACTRL_HRPROT_MASK                  0xf0000000
#define OUT2DMACTRL_HRPROT_GET(x)                (((x) & OUT2DMACTRL_HRPROT_MASK) >> OUT2DMACTRL_HRPROT_LSB)
#define OUT2DMACTRL_HRPROT_SET(x)                (((x) << OUT2DMACTRL_HRPROT_LSB) & OUT2DMACTRL_HRPROT_MASK)
#define OUT2DMACTRL_HSIZE_MSB                    27
#define OUT2DMACTRL_HSIZE_LSB                    26
#define OUT2DMACTRL_HSIZE_MASK                   0x0c000000
#define OUT2DMACTRL_HSIZE_GET(x)                 (((x) & OUT2DMACTRL_HSIZE_MASK) >> OUT2DMACTRL_HSIZE_LSB)
#define OUT2DMACTRL_HSIZE_SET(x)                 (((x) << OUT2DMACTRL_HSIZE_LSB) & OUT2DMACTRL_HSIZE_MASK)
#define OUT2DMACTRL_HLOCK_MSB                    25
#define OUT2DMACTRL_HLOCK_LSB                    25
#define OUT2DMACTRL_HLOCK_MASK                   0x02000000
#define OUT2DMACTRL_HLOCK_GET(x)                 (((x) & OUT2DMACTRL_HLOCK_MASK) >> OUT2DMACTRL_HLOCK_LSB)
#define OUT2DMACTRL_HLOCK_SET(x)                 (((x) << OUT2DMACTRL_HLOCK_LSB) & OUT2DMACTRL_HLOCK_MASK)
#define OUT2DMACTRL_DMARING_MSB                  22
#define OUT2DMACTRL_DMARING_LSB                  22
#define OUT2DMACTRL_DMARING_MASK                 0x00400000
#define OUT2DMACTRL_DMARING_GET(x)               (((x) & OUT2DMACTRL_DMARING_MASK) >> OUT2DMACTRL_DMARING_LSB)
#define OUT2DMACTRL_DMARING_SET(x)               (((x) << OUT2DMACTRL_DMARING_LSB) & OUT2DMACTRL_DMARING_MASK)
#define OUT2DMACTRL_DMANINCR_MSB                 21
#define OUT2DMACTRL_DMANINCR_LSB                 21
#define OUT2DMACTRL_DMANINCR_MASK                0x00200000
#define OUT2DMACTRL_DMANINCR_GET(x)              (((x) & OUT2DMACTRL_DMANINCR_MASK) >> OUT2DMACTRL_DMANINCR_LSB)
#define OUT2DMACTRL_DMANINCR_SET(x)              (((x) << OUT2DMACTRL_DMANINCR_LSB) & OUT2DMACTRL_DMANINCR_MASK)
#define OUT2DMACTRL_DMATUNLIM_MSB                20
#define OUT2DMACTRL_DMATUNLIM_LSB                20
#define OUT2DMACTRL_DMATUNLIM_MASK               0x00100000
#define OUT2DMACTRL_DMATUNLIM_GET(x)             (((x) & OUT2DMACTRL_DMATUNLIM_MASK) >> OUT2DMACTRL_DMATUNLIM_LSB)
#define OUT2DMACTRL_DMATUNLIM_SET(x)             (((x) << OUT2DMACTRL_DMATUNLIM_LSB) & OUT2DMACTRL_DMATUNLIM_MASK)
#define OUT2DMACTRL_DMASTART_MSB                 18
#define OUT2DMACTRL_DMASTART_LSB                 18
#define OUT2DMACTRL_DMASTART_MASK                0x00040000
#define OUT2DMACTRL_DMASTART_GET(x)              (((x) & OUT2DMACTRL_DMASTART_MASK) >> OUT2DMACTRL_DMASTART_LSB)
#define OUT2DMACTRL_DMASTART_SET(x)              (((x) << OUT2DMACTRL_DMASTART_LSB) & OUT2DMACTRL_DMASTART_MASK)
#define OUT2DMACTRL_DMASTOP_MSB                  17
#define OUT2DMACTRL_DMASTOP_LSB                  17
#define OUT2DMACTRL_DMASTOP_MASK                 0x00020000
#define OUT2DMACTRL_DMASTOP_GET(x)               (((x) & OUT2DMACTRL_DMASTOP_MASK) >> OUT2DMACTRL_DMASTOP_LSB)
#define OUT2DMACTRL_DMASTOP_SET(x)               (((x) << OUT2DMACTRL_DMASTOP_LSB) & OUT2DMACTRL_DMASTOP_MASK)
#define OUT2DMACTRL_ENDIAN_MSB                   16
#define OUT2DMACTRL_ENDIAN_LSB                   16
#define OUT2DMACTRL_ENDIAN_MASK                  0x00010000
#define OUT2DMACTRL_ENDIAN_GET(x)                (((x) & OUT2DMACTRL_ENDIAN_MASK) >> OUT2DMACTRL_ENDIAN_LSB)
#define OUT2DMACTRL_ENDIAN_SET(x)                (((x) << OUT2DMACTRL_ENDIAN_LSB) & OUT2DMACTRL_ENDIAN_MASK)
#define OUT2DMACTRL_RINGSIZ_MSB                  15
#define OUT2DMACTRL_RINGSIZ_LSB                  2
#define OUT2DMACTRL_RINGSIZ_MASK                 0x0000fffc
#define OUT2DMACTRL_RINGSIZ_GET(x)               (((x) & OUT2DMACTRL_RINGSIZ_MASK) >> OUT2DMACTRL_RINGSIZ_LSB)
#define OUT2DMACTRL_RINGSIZ_SET(x)               (((x) << OUT2DMACTRL_RINGSIZ_LSB) & OUT2DMACTRL_RINGSIZ_MASK)

#define EP3DMAADDR_ADDRESS                       0x00000460
#define EP3DMAADDR_OFFSET                        0x00000460
#define EP3DMAADDR_ADDR_MSB                      31
#define EP3DMAADDR_ADDR_LSB                      2
#define EP3DMAADDR_ADDR_MASK                     0xfffffffc
#define EP3DMAADDR_ADDR_GET(x)                   (((x) & EP3DMAADDR_ADDR_MASK) >> EP3DMAADDR_ADDR_LSB)
#define EP3DMAADDR_ADDR_SET(x)                   (((x) << EP3DMAADDR_ADDR_LSB) & EP3DMAADDR_ADDR_MASK)

#define OUT3DMACTRL_ADDRESS                      0x0000046c
#define OUT3DMACTRL_OFFSET                       0x0000046c
#define OUT3DMACTRL_HRPROT_MSB                   31
#define OUT3DMACTRL_HRPROT_LSB                   28
#define OUT3DMACTRL_HRPROT_MASK                  0xf0000000
#define OUT3DMACTRL_HRPROT_GET(x)                (((x) & OUT3DMACTRL_HRPROT_MASK) >> OUT3DMACTRL_HRPROT_LSB)
#define OUT3DMACTRL_HRPROT_SET(x)                (((x) << OUT3DMACTRL_HRPROT_LSB) & OUT3DMACTRL_HRPROT_MASK)
#define OUT3DMACTRL_HSIZE_MSB                    27
#define OUT3DMACTRL_HSIZE_LSB                    26
#define OUT3DMACTRL_HSIZE_MASK                   0x0c000000
#define OUT3DMACTRL_HSIZE_GET(x)                 (((x) & OUT3DMACTRL_HSIZE_MASK) >> OUT3DMACTRL_HSIZE_LSB)
#define OUT3DMACTRL_HSIZE_SET(x)                 (((x) << OUT3DMACTRL_HSIZE_LSB) & OUT3DMACTRL_HSIZE_MASK)
#define OUT3DMACTRL_HLOCK_MSB                    25
#define OUT3DMACTRL_HLOCK_LSB                    25
#define OUT3DMACTRL_HLOCK_MASK                   0x02000000
#define OUT3DMACTRL_HLOCK_GET(x)                 (((x) & OUT3DMACTRL_HLOCK_MASK) >> OUT3DMACTRL_HLOCK_LSB)
#define OUT3DMACTRL_HLOCK_SET(x)                 (((x) << OUT3DMACTRL_HLOCK_LSB) & OUT3DMACTRL_HLOCK_MASK)
#define OUT3DMACTRL_DMARING_MSB                  22
#define OUT3DMACTRL_DMARING_LSB                  22
#define OUT3DMACTRL_DMARING_MASK                 0x00400000
#define OUT3DMACTRL_DMARING_GET(x)               (((x) & OUT3DMACTRL_DMARING_MASK) >> OUT3DMACTRL_DMARING_LSB)
#define OUT3DMACTRL_DMARING_SET(x)               (((x) << OUT3DMACTRL_DMARING_LSB) & OUT3DMACTRL_DMARING_MASK)
#define OUT3DMACTRL_DMANINCR_MSB                 21
#define OUT3DMACTRL_DMANINCR_LSB                 21
#define OUT3DMACTRL_DMANINCR_MASK                0x00200000
#define OUT3DMACTRL_DMANINCR_GET(x)              (((x) & OUT3DMACTRL_DMANINCR_MASK) >> OUT3DMACTRL_DMANINCR_LSB)
#define OUT3DMACTRL_DMANINCR_SET(x)              (((x) << OUT3DMACTRL_DMANINCR_LSB) & OUT3DMACTRL_DMANINCR_MASK)
#define OUT3DMACTRL_DMATUNLIM_MSB                20
#define OUT3DMACTRL_DMATUNLIM_LSB                20
#define OUT3DMACTRL_DMATUNLIM_MASK               0x00100000
#define OUT3DMACTRL_DMATUNLIM_GET(x)             (((x) & OUT3DMACTRL_DMATUNLIM_MASK) >> OUT3DMACTRL_DMATUNLIM_LSB)
#define OUT3DMACTRL_DMATUNLIM_SET(x)             (((x) << OUT3DMACTRL_DMATUNLIM_LSB) & OUT3DMACTRL_DMATUNLIM_MASK)
#define OUT3DMACTRL_DMASTART_MSB                 18
#define OUT3DMACTRL_DMASTART_LSB                 18
#define OUT3DMACTRL_DMASTART_MASK                0x00040000
#define OUT3DMACTRL_DMASTART_GET(x)              (((x) & OUT3DMACTRL_DMASTART_MASK) >> OUT3DMACTRL_DMASTART_LSB)
#define OUT3DMACTRL_DMASTART_SET(x)              (((x) << OUT3DMACTRL_DMASTART_LSB) & OUT3DMACTRL_DMASTART_MASK)
#define OUT3DMACTRL_DMASTOP_MSB                  17
#define OUT3DMACTRL_DMASTOP_LSB                  17
#define OUT3DMACTRL_DMASTOP_MASK                 0x00020000
#define OUT3DMACTRL_DMASTOP_GET(x)               (((x) & OUT3DMACTRL_DMASTOP_MASK) >> OUT3DMACTRL_DMASTOP_LSB)
#define OUT3DMACTRL_DMASTOP_SET(x)               (((x) << OUT3DMACTRL_DMASTOP_LSB) & OUT3DMACTRL_DMASTOP_MASK)
#define OUT3DMACTRL_ENDIAN_MSB                   16
#define OUT3DMACTRL_ENDIAN_LSB                   16
#define OUT3DMACTRL_ENDIAN_MASK                  0x00010000
#define OUT3DMACTRL_ENDIAN_GET(x)                (((x) & OUT3DMACTRL_ENDIAN_MASK) >> OUT3DMACTRL_ENDIAN_LSB)
#define OUT3DMACTRL_ENDIAN_SET(x)                (((x) << OUT3DMACTRL_ENDIAN_LSB) & OUT3DMACTRL_ENDIAN_MASK)
#define OUT3DMACTRL_RINGSIZ_MSB                  15
#define OUT3DMACTRL_RINGSIZ_LSB                  2
#define OUT3DMACTRL_RINGSIZ_MASK                 0x0000fffc
#define OUT3DMACTRL_RINGSIZ_GET(x)               (((x) & OUT3DMACTRL_RINGSIZ_MASK) >> OUT3DMACTRL_RINGSIZ_LSB)
#define OUT3DMACTRL_RINGSIZ_SET(x)               (((x) << OUT3DMACTRL_RINGSIZ_LSB) & OUT3DMACTRL_RINGSIZ_MASK)

#define EP4DMAADDR_ADDRESS                       0x00000480
#define EP4DMAADDR_OFFSET                        0x00000480
#define EP4DMAADDR_ADDR_MSB                      31
#define EP4DMAADDR_ADDR_LSB                      2
#define EP4DMAADDR_ADDR_MASK                     0xfffffffc
#define EP4DMAADDR_ADDR_GET(x)                   (((x) & EP4DMAADDR_ADDR_MASK) >> EP4DMAADDR_ADDR_LSB)
#define EP4DMAADDR_ADDR_SET(x)                   (((x) << EP4DMAADDR_ADDR_LSB) & EP4DMAADDR_ADDR_MASK)

#define OUT4DMACTRL_ADDRESS                      0x0000048c
#define OUT4DMACTRL_OFFSET                       0x0000048c
#define OUT4DMACTRL_HRPROT_MSB                   31
#define OUT4DMACTRL_HRPROT_LSB                   28
#define OUT4DMACTRL_HRPROT_MASK                  0xf0000000
#define OUT4DMACTRL_HRPROT_GET(x)                (((x) & OUT4DMACTRL_HRPROT_MASK) >> OUT4DMACTRL_HRPROT_LSB)
#define OUT4DMACTRL_HRPROT_SET(x)                (((x) << OUT4DMACTRL_HRPROT_LSB) & OUT4DMACTRL_HRPROT_MASK)
#define OUT4DMACTRL_HSIZE_MSB                    27
#define OUT4DMACTRL_HSIZE_LSB                    26
#define OUT4DMACTRL_HSIZE_MASK                   0x0c000000
#define OUT4DMACTRL_HSIZE_GET(x)                 (((x) & OUT4DMACTRL_HSIZE_MASK) >> OUT4DMACTRL_HSIZE_LSB)
#define OUT4DMACTRL_HSIZE_SET(x)                 (((x) << OUT4DMACTRL_HSIZE_LSB) & OUT4DMACTRL_HSIZE_MASK)
#define OUT4DMACTRL_HLOCK_MSB                    25
#define OUT4DMACTRL_HLOCK_LSB                    25
#define OUT4DMACTRL_HLOCK_MASK                   0x02000000
#define OUT4DMACTRL_HLOCK_GET(x)                 (((x) & OUT4DMACTRL_HLOCK_MASK) >> OUT4DMACTRL_HLOCK_LSB)
#define OUT4DMACTRL_HLOCK_SET(x)                 (((x) << OUT4DMACTRL_HLOCK_LSB) & OUT4DMACTRL_HLOCK_MASK)
#define OUT4DMACTRL_DMARING_MSB                  22
#define OUT4DMACTRL_DMARING_LSB                  22
#define OUT4DMACTRL_DMARING_MASK                 0x00400000
#define OUT4DMACTRL_DMARING_GET(x)               (((x) & OUT4DMACTRL_DMARING_MASK) >> OUT4DMACTRL_DMARING_LSB)
#define OUT4DMACTRL_DMARING_SET(x)               (((x) << OUT4DMACTRL_DMARING_LSB) & OUT4DMACTRL_DMARING_MASK)
#define OUT4DMACTRL_DMANINCR_MSB                 21
#define OUT4DMACTRL_DMANINCR_LSB                 21
#define OUT4DMACTRL_DMANINCR_MASK                0x00200000
#define OUT4DMACTRL_DMANINCR_GET(x)              (((x) & OUT4DMACTRL_DMANINCR_MASK) >> OUT4DMACTRL_DMANINCR_LSB)
#define OUT4DMACTRL_DMANINCR_SET(x)              (((x) << OUT4DMACTRL_DMANINCR_LSB) & OUT4DMACTRL_DMANINCR_MASK)
#define OUT4DMACTRL_DMATUNLIM_MSB                20
#define OUT4DMACTRL_DMATUNLIM_LSB                20
#define OUT4DMACTRL_DMATUNLIM_MASK               0x00100000
#define OUT4DMACTRL_DMATUNLIM_GET(x)             (((x) & OUT4DMACTRL_DMATUNLIM_MASK) >> OUT4DMACTRL_DMATUNLIM_LSB)
#define OUT4DMACTRL_DMATUNLIM_SET(x)             (((x) << OUT4DMACTRL_DMATUNLIM_LSB) & OUT4DMACTRL_DMATUNLIM_MASK)
#define OUT4DMACTRL_DMASTART_MSB                 18
#define OUT4DMACTRL_DMASTART_LSB                 18
#define OUT4DMACTRL_DMASTART_MASK                0x00040000
#define OUT4DMACTRL_DMASTART_GET(x)              (((x) & OUT4DMACTRL_DMASTART_MASK) >> OUT4DMACTRL_DMASTART_LSB)
#define OUT4DMACTRL_DMASTART_SET(x)              (((x) << OUT4DMACTRL_DMASTART_LSB) & OUT4DMACTRL_DMASTART_MASK)
#define OUT4DMACTRL_DMASTOP_MSB                  17
#define OUT4DMACTRL_DMASTOP_LSB                  17
#define OUT4DMACTRL_DMASTOP_MASK                 0x00020000
#define OUT4DMACTRL_DMASTOP_GET(x)               (((x) & OUT4DMACTRL_DMASTOP_MASK) >> OUT4DMACTRL_DMASTOP_LSB)
#define OUT4DMACTRL_DMASTOP_SET(x)               (((x) << OUT4DMACTRL_DMASTOP_LSB) & OUT4DMACTRL_DMASTOP_MASK)
#define OUT4DMACTRL_ENDIAN_MSB                   16
#define OUT4DMACTRL_ENDIAN_LSB                   16
#define OUT4DMACTRL_ENDIAN_MASK                  0x00010000
#define OUT4DMACTRL_ENDIAN_GET(x)                (((x) & OUT4DMACTRL_ENDIAN_MASK) >> OUT4DMACTRL_ENDIAN_LSB)
#define OUT4DMACTRL_ENDIAN_SET(x)                (((x) << OUT4DMACTRL_ENDIAN_LSB) & OUT4DMACTRL_ENDIAN_MASK)
#define OUT4DMACTRL_RINGSIZ_MSB                  15
#define OUT4DMACTRL_RINGSIZ_LSB                  2
#define OUT4DMACTRL_RINGSIZ_MASK                 0x0000fffc
#define OUT4DMACTRL_RINGSIZ_GET(x)               (((x) & OUT4DMACTRL_RINGSIZ_MASK) >> OUT4DMACTRL_RINGSIZ_LSB)
#define OUT4DMACTRL_RINGSIZ_SET(x)               (((x) << OUT4DMACTRL_RINGSIZ_LSB) & OUT4DMACTRL_RINGSIZ_MASK)

#define EP5DMAADDR_ADDRESS                       0x000004a0
#define EP5DMAADDR_OFFSET                        0x000004a0
#define EP5DMAADDR_ADDR_MSB                      31
#define EP5DMAADDR_ADDR_LSB                      2
#define EP5DMAADDR_ADDR_MASK                     0xfffffffc
#define EP5DMAADDR_ADDR_GET(x)                   (((x) & EP5DMAADDR_ADDR_MASK) >> EP5DMAADDR_ADDR_LSB)
#define EP5DMAADDR_ADDR_SET(x)                   (((x) << EP5DMAADDR_ADDR_LSB) & EP5DMAADDR_ADDR_MASK)

#define OUT5DMACTRL_ADDRESS                      0x000004ac
#define OUT5DMACTRL_OFFSET                       0x000004ac
#define OUT5DMACTRL_HRPROT_MSB                   31
#define OUT5DMACTRL_HRPROT_LSB                   28
#define OUT5DMACTRL_HRPROT_MASK                  0xf0000000
#define OUT5DMACTRL_HRPROT_GET(x)                (((x) & OUT5DMACTRL_HRPROT_MASK) >> OUT5DMACTRL_HRPROT_LSB)
#define OUT5DMACTRL_HRPROT_SET(x)                (((x) << OUT5DMACTRL_HRPROT_LSB) & OUT5DMACTRL_HRPROT_MASK)
#define OUT5DMACTRL_HSIZE_MSB                    27
#define OUT5DMACTRL_HSIZE_LSB                    26
#define OUT5DMACTRL_HSIZE_MASK                   0x0c000000
#define OUT5DMACTRL_HSIZE_GET(x)                 (((x) & OUT5DMACTRL_HSIZE_MASK) >> OUT5DMACTRL_HSIZE_LSB)
#define OUT5DMACTRL_HSIZE_SET(x)                 (((x) << OUT5DMACTRL_HSIZE_LSB) & OUT5DMACTRL_HSIZE_MASK)
#define OUT5DMACTRL_HLOCK_MSB                    25
#define OUT5DMACTRL_HLOCK_LSB                    25
#define OUT5DMACTRL_HLOCK_MASK                   0x02000000
#define OUT5DMACTRL_HLOCK_GET(x)                 (((x) & OUT5DMACTRL_HLOCK_MASK) >> OUT5DMACTRL_HLOCK_LSB)
#define OUT5DMACTRL_HLOCK_SET(x)                 (((x) << OUT5DMACTRL_HLOCK_LSB) & OUT5DMACTRL_HLOCK_MASK)
#define OUT5DMACTRL_DMARING_MSB                  22
#define OUT5DMACTRL_DMARING_LSB                  22
#define OUT5DMACTRL_DMARING_MASK                 0x00400000
#define OUT5DMACTRL_DMARING_GET(x)               (((x) & OUT5DMACTRL_DMARING_MASK) >> OUT5DMACTRL_DMARING_LSB)
#define OUT5DMACTRL_DMARING_SET(x)               (((x) << OUT5DMACTRL_DMARING_LSB) & OUT5DMACTRL_DMARING_MASK)
#define OUT5DMACTRL_DMANINCR_MSB                 21
#define OUT5DMACTRL_DMANINCR_LSB                 21
#define OUT5DMACTRL_DMANINCR_MASK                0x00200000
#define OUT5DMACTRL_DMANINCR_GET(x)              (((x) & OUT5DMACTRL_DMANINCR_MASK) >> OUT5DMACTRL_DMANINCR_LSB)
#define OUT5DMACTRL_DMANINCR_SET(x)              (((x) << OUT5DMACTRL_DMANINCR_LSB) & OUT5DMACTRL_DMANINCR_MASK)
#define OUT5DMACTRL_DMATUNLIM_MSB                20
#define OUT5DMACTRL_DMATUNLIM_LSB                20
#define OUT5DMACTRL_DMATUNLIM_MASK               0x00100000
#define OUT5DMACTRL_DMATUNLIM_GET(x)             (((x) & OUT5DMACTRL_DMATUNLIM_MASK) >> OUT5DMACTRL_DMATUNLIM_LSB)
#define OUT5DMACTRL_DMATUNLIM_SET(x)             (((x) << OUT5DMACTRL_DMATUNLIM_LSB) & OUT5DMACTRL_DMATUNLIM_MASK)
#define OUT5DMACTRL_DMASTART_MSB                 18
#define OUT5DMACTRL_DMASTART_LSB                 18
#define OUT5DMACTRL_DMASTART_MASK                0x00040000
#define OUT5DMACTRL_DMASTART_GET(x)              (((x) & OUT5DMACTRL_DMASTART_MASK) >> OUT5DMACTRL_DMASTART_LSB)
#define OUT5DMACTRL_DMASTART_SET(x)              (((x) << OUT5DMACTRL_DMASTART_LSB) & OUT5DMACTRL_DMASTART_MASK)
#define OUT5DMACTRL_DMASTOP_MSB                  17
#define OUT5DMACTRL_DMASTOP_LSB                  17
#define OUT5DMACTRL_DMASTOP_MASK                 0x00020000
#define OUT5DMACTRL_DMASTOP_GET(x)               (((x) & OUT5DMACTRL_DMASTOP_MASK) >> OUT5DMACTRL_DMASTOP_LSB)
#define OUT5DMACTRL_DMASTOP_SET(x)               (((x) << OUT5DMACTRL_DMASTOP_LSB) & OUT5DMACTRL_DMASTOP_MASK)
#define OUT5DMACTRL_ENDIAN_MSB                   16
#define OUT5DMACTRL_ENDIAN_LSB                   16
#define OUT5DMACTRL_ENDIAN_MASK                  0x00010000
#define OUT5DMACTRL_ENDIAN_GET(x)                (((x) & OUT5DMACTRL_ENDIAN_MASK) >> OUT5DMACTRL_ENDIAN_LSB)
#define OUT5DMACTRL_ENDIAN_SET(x)                (((x) << OUT5DMACTRL_ENDIAN_LSB) & OUT5DMACTRL_ENDIAN_MASK)
#define OUT5DMACTRL_RINGSIZ_MSB                  15
#define OUT5DMACTRL_RINGSIZ_LSB                  2
#define OUT5DMACTRL_RINGSIZ_MASK                 0x0000fffc
#define OUT5DMACTRL_RINGSIZ_GET(x)               (((x) & OUT5DMACTRL_RINGSIZ_MASK) >> OUT5DMACTRL_RINGSIZ_LSB)
#define OUT5DMACTRL_RINGSIZ_SET(x)               (((x) << OUT5DMACTRL_RINGSIZ_LSB) & OUT5DMACTRL_RINGSIZ_MASK)

#define USB_IP_BASE_ADDRESS                      0x00084000
#define USB_IP_BASE_OFFSET                       0x00084000


#ifndef __ASSEMBLER__

typedef struct usb_cast_reg_reg_s {
  volatile unsigned int endp0;
  unsigned char pad0[4]; /* pad to 0x8 */
  volatile unsigned int out1endp;
  volatile unsigned int in1endp;
  volatile unsigned int out2endp;
  volatile unsigned int in2endp;
  volatile unsigned int out3endp;
  volatile unsigned int in3endp;
  volatile unsigned int out4endp;
  volatile unsigned int in4endp;
  volatile unsigned int out5endp;
  volatile unsigned int in5endp;
  unsigned char pad1[92]; /* pad to 0x8c */
  volatile unsigned int usbmodestatus;
  unsigned char pad2[248]; /* pad to 0x188 */
  volatile unsigned int epirq;
  volatile unsigned int usbirq;
  unsigned char pad3[4]; /* pad to 0x194 */
  volatile unsigned int epien;
  volatile unsigned int pien;
  unsigned char pad4[8]; /* pad to 0x1a4 */
  volatile unsigned int fnctrl;
  unsigned char pad5[20]; /* pad to 0x1bc */
  volatile unsigned int otgreg;
  unsigned char pad6[12]; /* pad to 0x1cc */
  volatile unsigned int dmastart;
  volatile unsigned int dmastop;
  unsigned char pad7[556]; /* pad to 0x400 */
  volatile unsigned int ep0dmaaddr;
  unsigned char pad8[28]; /* pad to 0x420 */
  volatile unsigned int ep1dmaaddr;
  unsigned char pad9[8]; /* pad to 0x42c */
  volatile unsigned int out1dmactrl;
  unsigned char pad10[16]; /* pad to 0x440 */
  volatile unsigned int ep2dmaaddr;
  unsigned char pad11[8]; /* pad to 0x44c */
  volatile unsigned int out2dmactrl;
  unsigned char pad12[16]; /* pad to 0x460 */
  volatile unsigned int ep3dmaaddr;
  unsigned char pad13[8]; /* pad to 0x46c */
  volatile unsigned int out3dmactrl;
  unsigned char pad14[16]; /* pad to 0x480 */
  volatile unsigned int ep4dmaaddr;
  unsigned char pad15[8]; /* pad to 0x48c */
  volatile unsigned int out4dmactrl;
  unsigned char pad16[16]; /* pad to 0x4a0 */
  volatile unsigned int ep5dmaaddr;
  unsigned char pad17[8]; /* pad to 0x4ac */
  volatile unsigned int out5dmactrl;
  unsigned char pad18[539472]; /* pad to 0x84000 */
  volatile unsigned int usb_ip_base;
} usb_cast_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _USB_CAST_REG_H_ */
