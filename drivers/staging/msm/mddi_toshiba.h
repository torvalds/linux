/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef MDDI_TOSHIBA_H
#define MDDI_TOSHIBA_H

#define TOSHIBA_VGA_PRIM 1
#define TOSHIBA_VGA_SECD 2

#define LCD_TOSHIBA_2P4_VGA 	0
#define LCD_TOSHIBA_2P4_WVGA 	1
#define LCD_TOSHIBA_2P4_WVGA_PT	2
#define LCD_SHARP_2P4_VGA 	3

#define GPIO_BLOCK_BASE        0x150000
#define SYSTEM_BLOCK2_BASE     0x170000

#define GPIODIR     (GPIO_BLOCK_BASE|0x04)
#define GPIOSEL     (SYSTEM_BLOCK2_BASE|0x00)
#define GPIOPC      (GPIO_BLOCK_BASE|0x28)
#define GPIODATA    (GPIO_BLOCK_BASE|0x00)

#define write_client_reg(__X, __Y, __Z) {\
  mddi_queue_register_write(__X, __Y, TRUE, 0);\
}

#endif /* MDDI_TOSHIBA_H */
