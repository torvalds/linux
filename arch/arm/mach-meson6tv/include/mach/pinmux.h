//      pinmux.h
//
//      Copyright 2011 Unknown <jerry.yu@jerryyu-Lenovo>
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//      MA 02110-1301, USA.
//
//
#ifndef __INCLUDE_MACH_MESON3_H_
#define __INCLUDE_MACH_MESON3_H_
#include <linux/types.h>
#define AO				10
#define PINMUX_REG(n)	n
#define PINMUX_END_ITEM {.reg=0xffffffff}



/**
 * structure and data type
 */
typedef struct __pinmux_item{
	uint32_t reg;
	uint32_t clrmask;
	uint32_t setmask;
}pinmux_item_t;
struct __pinmux_set_s{
	bool (* chip_select)(bool);///@todo implement it later .
	pinmux_item_t * pinmux;
};

typedef struct __pinmux_set_s pinmux_set_t;

/**
 * UTIL interface
 * these function can be implement in a tools
 */
 /**
  * @return NULL is fail
  * 		errno NOTAVAILABLE ,
  * 			  SOMEPIN IS LOCKED
  */
pinmux_set_t* pinmux_cacl_str(char * pad,char * sig ,...);
pinmux_set_t* pinmux_cacl_int(uint32_t pad,uint32_t sig ,...);
pinmux_set_t* pinmux_cacl(char * str);///formate is "pad=sig pad=sig "
char ** pin_get_list(void);
char ** sig_get_list(void);
char * pin_getname(uint32_t pin);
char * sig_getname(uint32_t sig);
uint32_t pins_num(void);
/**
 * Util Get status function
 */
uint32_t pin_sig(uint32_t pin);
uint32_t sig_pin(uint32_t sig);
/**
 * pinmux set function
 * @return 0, success ,
 * 		   SOMEPIN IS LOCKED, some pin is locked to the specail feature . You can not change it
 * 		   NOTAVAILABLE, not available .
 */
int32_t pinmux_set(pinmux_set_t *);
int32_t pinmux_clr(pinmux_set_t *);
int32_t pinmux_set_locktable(pinmux_set_t*);
#endif
