/*
   Driver for the SAA5246A or SAA5281 Teletext (=Videotext) decoder chips from
   Philips.

   Copyright (C) 2004 Michael Geng (linux@MichaelGeng.de)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */
#ifndef __SAA5246A_H__
#define __SAA5246A_H__

#define MAJOR_VERSION 1		/* driver major version number */
#define MINOR_VERSION 8		/* driver minor version number */

#define IF_NAME "SAA5246A"

#define I2C_ADDRESS 17

/* Number of DAUs = number of pages that can be searched at the same time. */
#define NUM_DAUS 4

#define NUM_ROWS_PER_PAGE 40

/* first column is 0 (not 1) */
#define POS_TIME_START 32
#define POS_TIME_END 39

#define POS_HEADER_START 7
#define POS_HEADER_END 31

/* Returns 'true' if the part of the videotext page described with req contains
   (at least parts of) the time field */
#define REQ_CONTAINS_TIME(p_req) \
	((p_req)->start <= POS_TIME_END && \
	 (p_req)->end   >= POS_TIME_START)

/* Returns 'true' if the part of the videotext page described with req contains
   (at least parts of) the page header */
#define REQ_CONTAINS_HEADER(p_req) \
	((p_req)->start <= POS_HEADER_END && \
	 (p_req)->end   >= POS_HEADER_START)

/*****************************************************************************/
/* Mode register numbers of the SAA5246A				     */
/*****************************************************************************/
#define SAA5246A_REGISTER_R0    0
#define SAA5246A_REGISTER_R1    1
#define SAA5246A_REGISTER_R2    2
#define SAA5246A_REGISTER_R3    3
#define SAA5246A_REGISTER_R4    4
#define SAA5246A_REGISTER_R5    5
#define SAA5246A_REGISTER_R6    6
#define SAA5246A_REGISTER_R7    7
#define SAA5246A_REGISTER_R8    8
#define SAA5246A_REGISTER_R9    9
#define SAA5246A_REGISTER_R10  10
#define SAA5246A_REGISTER_R11  11
#define SAA5246A_REGISTER_R11B 11

/* SAA5246A mode registers often autoincrement to the next register.
   Therefore we use variable argument lists. The following macro indicates
   the end of a command list. */
#define COMMAND_END (- 1)

/*****************************************************************************/
/* Contents of the mode registers of the SAA5246A			     */
/*****************************************************************************/
/* Register R0 (Advanced Control) */
#define R0_SELECT_R11					   0x00
#define R0_SELECT_R11B					   0x01

#define R0_PLL_TIME_CONSTANT_LONG			   0x00
#define R0_PLL_TIME_CONSTANT_SHORT			   0x02

#define R0_ENABLE_nODD_EVEN_OUTPUT			   0x00
#define R0_DISABLE_nODD_EVEN_OUTPUT			   0x04

#define R0_ENABLE_HDR_POLL				   0x00
#define R0_DISABLE_HDR_POLL				   0x10

#define R0_DO_NOT_FORCE_nODD_EVEN_LOW_IF_PICTURE_DISPLAYED 0x00
#define R0_FORCE_nODD_EVEN_LOW_IF_PICTURE_DISPLAYED	   0x20

#define R0_NO_FREE_RUN_PLL				   0x00
#define R0_FREE_RUN_PLL					   0x40

#define R0_NO_AUTOMATIC_FASTEXT_PROMPT			   0x00
#define R0_AUTOMATIC_FASTEXT_PROMPT			   0x80

/* Register R1 (Mode) */
#define R1_INTERLACED_312_AND_HALF_312_AND_HALF_LINES	   0x00
#define R1_NON_INTERLACED_312_313_LINES			   0x01
#define R1_NON_INTERLACED_312_312_LINES			   0x02
#define R1_FFB_LEADING_EDGE_IN_FIRST_BROAD_PULSE	   0x03
#define R1_FFB_LEADING_EDGE_IN_SECOND_BROAD_PULSE	   0x07

#define R1_DEW						   0x00
#define R1_FULL_FIELD					   0x08

#define R1_EXTENDED_PACKET_DISABLE			   0x00
#define R1_EXTENDED_PACKET_ENABLE			   0x10

#define R1_DAUS_ALL_ON					   0x00
#define R1_DAUS_ALL_OFF					   0x20

#define R1_7_BITS_PLUS_PARITY				   0x00
#define R1_8_BITS_NO_PARITY				   0x40

#define R1_VCS_TO_SCS					   0x00
#define R1_NO_VCS_TO_SCS				   0x80

/* Register R2 (Page request address) */
#define R2_IN_R3_SELECT_PAGE_HUNDREDS			   0x00
#define R2_IN_R3_SELECT_PAGE_TENS			   0x01
#define R2_IN_R3_SELECT_PAGE_UNITS			   0x02
#define R2_IN_R3_SELECT_HOURS_TENS			   0x03
#define R2_IN_R3_SELECT_HOURS_UNITS			   0x04
#define R2_IN_R3_SELECT_MINUTES_TENS			   0x05
#define R2_IN_R3_SELECT_MINUTES_UNITS			   0x06

#define R2_DAU_0					   0x00
#define R2_DAU_1					   0x10
#define R2_DAU_2					   0x20
#define R2_DAU_3					   0x30

#define R2_BANK_0					   0x00
#define R2_BANK 1					   0x40

#define R2_HAMMING_CHECK_ON				   0x80
#define R2_HAMMING_CHECK_OFF				   0x00

/* Register R3 (Page request data) */
#define R3_PAGE_HUNDREDS_0				   0x00
#define R3_PAGE_HUNDREDS_1				   0x01
#define R3_PAGE_HUNDREDS_2				   0x02
#define R3_PAGE_HUNDREDS_3				   0x03
#define R3_PAGE_HUNDREDS_4				   0x04
#define R3_PAGE_HUNDREDS_5				   0x05
#define R3_PAGE_HUNDREDS_6				   0x06
#define R3_PAGE_HUNDREDS_7				   0x07

#define R3_HOLD_PAGE					   0x00
#define R3_UPDATE_PAGE					   0x08

#define R3_PAGE_HUNDREDS_DO_NOT_CARE			   0x00
#define R3_PAGE_HUNDREDS_DO_CARE			   0x10

#define R3_PAGE_TENS_DO_NOT_CARE			   0x00
#define R3_PAGE_TENS_DO_CARE				   0x10

#define R3_PAGE_UNITS_DO_NOT_CARE			   0x00
#define R3_PAGE_UNITS_DO_CARE				   0x10

#define R3_HOURS_TENS_DO_NOT_CARE			   0x00
#define R3_HOURS_TENS_DO_CARE				   0x10

#define R3_HOURS_UNITS_DO_NOT_CARE			   0x00
#define R3_HOURS_UNITS_DO_CARE				   0x10

#define R3_MINUTES_TENS_DO_NOT_CARE			   0x00
#define R3_MINUTES_TENS_DO_CARE				   0x10

#define R3_MINUTES_UNITS_DO_NOT_CARE			   0x00
#define R3_MINUTES_UNITS_DO_CARE			   0x10

/* Register R4 (Display chapter) */
#define R4_DISPLAY_PAGE_0				   0x00
#define R4_DISPLAY_PAGE_1				   0x01
#define R4_DISPLAY_PAGE_2				   0x02
#define R4_DISPLAY_PAGE_3				   0x03
#define R4_DISPLAY_PAGE_4				   0x04
#define R4_DISPLAY_PAGE_5				   0x05
#define R4_DISPLAY_PAGE_6				   0x06
#define R4_DISPLAY_PAGE_7				   0x07

/* Register R5 (Normal display control) */
#define R5_PICTURE_INSIDE_BOXING_OFF			   0x00
#define R5_PICTURE_INSIDE_BOXING_ON			   0x01

#define R5_PICTURE_OUTSIDE_BOXING_OFF			   0x00
#define R5_PICTURE_OUTSIDE_BOXING_ON			   0x02

#define R5_TEXT_INSIDE_BOXING_OFF			   0x00
#define R5_TEXT_INSIDE_BOXING_ON			   0x04

#define R5_TEXT_OUTSIDE_BOXING_OFF			   0x00
#define R5_TEXT_OUTSIDE_BOXING_ON			   0x08

#define R5_CONTRAST_REDUCTION_INSIDE_BOXING_OFF		   0x00
#define R5_CONTRAST_REDUCTION_INSIDE_BOXING_ON		   0x10

#define R5_CONTRAST_REDUCTION_OUTSIDE_BOXING_OFF	   0x00
#define R5_CONTRAST_REDUCTION_OUTSIDE_BOXING_ON		   0x20

#define R5_BACKGROUND_COLOR_INSIDE_BOXING_OFF		   0x00
#define R5_BACKGROUND_COLOR_INSIDE_BOXING_ON		   0x40

#define R5_BACKGROUND_COLOR_OUTSIDE_BOXING_OFF		   0x00
#define R5_BACKGROUND_COLOR_OUTSIDE_BOXING_ON		   0x80

/* Register R6 (Newsflash display) */
#define R6_NEWSFLASH_PICTURE_INSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_PICTURE_INSIDE_BOXING_ON		   0x01

#define R6_NEWSFLASH_PICTURE_OUTSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_PICTURE_OUTSIDE_BOXING_ON		   0x02

#define R6_NEWSFLASH_TEXT_INSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_TEXT_INSIDE_BOXING_ON		   0x04

#define R6_NEWSFLASH_TEXT_OUTSIDE_BOXING_OFF		   0x00
#define R6_NEWSFLASH_TEXT_OUTSIDE_BOXING_ON		   0x08

#define R6_NEWSFLASH_CONTRAST_REDUCTION_INSIDE_BOXING_OFF  0x00
#define R6_NEWSFLASH_CONTRAST_REDUCTION_INSIDE_BOXING_ON   0x10

#define R6_NEWSFLASH_CONTRAST_REDUCTION_OUTSIDE_BOXING_OFF 0x00
#define R6_NEWSFLASH_CONTRAST_REDUCTION_OUTSIDE_BOXING_ON  0x20

#define R6_NEWSFLASH_BACKGROUND_COLOR_INSIDE_BOXING_OFF    0x00
#define R6_NEWSFLASH_BACKGROUND_COLOR_INSIDE_BOXING_ON	   0x40

#define R6_NEWSFLASH_BACKGROUND_COLOR_OUTSIDE_BOXING_OFF   0x00
#define R6_NEWSFLASH_BACKGROUND_COLOR_OUTSIDE_BOXING_ON    0x80

/* Register R7 (Display mode) */
#define R7_BOX_OFF_ROW_0				   0x00
#define R7_BOX_ON_ROW_0					   0x01

#define R7_BOX_OFF_ROW_1_TO_23				   0x00
#define R7_BOX_ON_ROW_1_TO_23				   0x02

#define R7_BOX_OFF_ROW_24				   0x00
#define R7_BOX_ON_ROW_24				   0x04

#define R7_SINGLE_HEIGHT				   0x00
#define R7_DOUBLE_HEIGHT				   0x08

#define R7_TOP_HALF					   0x00
#define R7_BOTTOM_HALF					   0x10

#define R7_REVEAL_OFF					   0x00
#define R7_REVEAL_ON					   0x20

#define R7_CURSER_OFF					   0x00
#define R7_CURSER_ON					   0x40

#define R7_STATUS_BOTTOM				   0x00
#define R7_STATUS_TOP					   0x80

/* Register R8 (Active chapter) */
#define R8_ACTIVE_CHAPTER_0				   0x00
#define R8_ACTIVE_CHAPTER_1				   0x01
#define R8_ACTIVE_CHAPTER_2				   0x02
#define R8_ACTIVE_CHAPTER_3				   0x03
#define R8_ACTIVE_CHAPTER_4				   0x04
#define R8_ACTIVE_CHAPTER_5				   0x05
#define R8_ACTIVE_CHAPTER_6				   0x06
#define R8_ACTIVE_CHAPTER_7				   0x07

#define R8_CLEAR_MEMORY					   0x08
#define R8_DO_NOT_CLEAR_MEMORY				   0x00

/* Register R9 (Curser row) */
#define R9_CURSER_ROW_0					   0x00
#define R9_CURSER_ROW_1					   0x01
#define R9_CURSER_ROW_2					   0x02
#define R9_CURSER_ROW_25				   0x19

/* Register R10 (Curser column) */
#define R10_CURSER_COLUMN_0				   0x00
#define R10_CURSER_COLUMN_6				   0x06
#define R10_CURSER_COLUMN_8				   0x08

/*****************************************************************************/
/* Row 25 control data in column 0 to 9					     */
/*****************************************************************************/
#define ROW25_COLUMN0_PAGE_UNITS			   0x0F

#define ROW25_COLUMN1_PAGE_TENS				   0x0F

#define ROW25_COLUMN2_MINUTES_UNITS			   0x0F

#define ROW25_COLUMN3_MINUTES_TENS			   0x07
#define ROW25_COLUMN3_DELETE_PAGE			   0x08

#define ROW25_COLUMN4_HOUR_UNITS			   0x0F

#define ROW25_COLUMN5_HOUR_TENS				   0x03
#define ROW25_COLUMN5_INSERT_HEADLINE			   0x04
#define ROW25_COLUMN5_INSERT_SUBTITLE			   0x08

#define ROW25_COLUMN6_SUPPRESS_HEADER			   0x01
#define ROW25_COLUMN6_UPDATE_PAGE			   0x02
#define ROW25_COLUMN6_INTERRUPTED_SEQUENCE		   0x04
#define ROW25_COLUMN6_SUPPRESS_DISPLAY			   0x08

#define ROW25_COLUMN7_SERIAL_MODE			   0x01
#define ROW25_COLUMN7_CHARACTER_SET			   0x0E

#define ROW25_COLUMN8_PAGE_HUNDREDS			   0x07
#define ROW25_COLUMN8_PAGE_NOT_FOUND			   0x10

#define ROW25_COLUMN9_PAGE_BEING_LOOKED_FOR		   0x20

#define ROW25_COLUMN0_TO_7_HAMMING_ERROR		   0x10

/*****************************************************************************/
/* Helper macros for extracting page, hour and minute digits		     */
/*****************************************************************************/
/* BYTE_POS  0 is at row 0, column 0,
   BYTE_POS  1 is at row 0, column 1,
   BYTE_POS 40 is at row 1, column 0, (with NUM_ROWS_PER_PAGE = 40)
   BYTE_POS 41 is at row 1, column 1, (with NUM_ROWS_PER_PAGE = 40),
   ... */
#define ROW(BYTE_POS)    (BYTE_POS / NUM_ROWS_PER_PAGE)
#define COLUMN(BYTE_POS) (BYTE_POS % NUM_ROWS_PER_PAGE)

/*****************************************************************************/
/* Helper macros for extracting page, hour and minute digits		     */
/*****************************************************************************/
/* Macros for extracting hundreds, tens and units of a page number which
   must be in the range 0 ... 0x799.
   Note that page is coded in hexadecimal, i.e. 0x123 means page 123.
   page 0x.. means page 8.. */
#define HUNDREDS_OF_PAGE(page) (((page) / 0x100) & 0x7)
#define TENS_OF_PAGE(page)     (((page) / 0x10)  & 0xF)
#define UNITS_OF_PAGE(page)     ((page) & 0xF)

/* Macros for extracting tens and units of a hour information which
   must be in the range 0 ... 0x24.
   Note that hour is coded in hexadecimal, i.e. 0x12 means 12 hours */
#define TENS_OF_HOUR(hour)  ((hour) / 0x10)
#define UNITS_OF_HOUR(hour) ((hour) & 0xF)

/* Macros for extracting tens and units of a minute information which
   must be in the range 0 ... 0x59.
   Note that minute is coded in hexadecimal, i.e. 0x12 means 12 minutes */
#define TENS_OF_MINUTE(minute)  ((minute) / 0x10)
#define UNITS_OF_MINUTE(minute) ((minute) & 0xF)

#define HOUR_MAX   0x23
#define MINUTE_MAX 0x59
#define PAGE_MAX   0x8FF

#endif  /* __SAA5246A_H__ */
