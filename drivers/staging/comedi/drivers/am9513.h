/*
    module/am9513.h
    value added preprocessor definitions for Am9513 timer chip

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>

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

#ifndef _AM9513_H_
#define _AM9513_H_

#if 0

/*
 *	Before including this file, the following need to be defined:
 */
#define Am9513_8BITBUS xxx
/* or */
#define Am9513_16BITBUS xxx

#define Am9513_output_control(a)	xxx
#define Am9513_input_status()		xxx
#define Am9513_output_data(a)		xxx
#define Am9513_input_data()		xxx

#endif

/*
 *
 */

#ifdef Am9513_8BITBUS

#define Am9513_write_register(reg,val)				\
	do{							\
		Am9513_output_control(reg);			\
		Am9513_output_data(val>>8);			\
		Am9513_output_data(val&0xff);			\
	}while(0)

#define Am9513_read_register(reg,val)				\
	do{							\
		Am9513_output_control(reg);			\
		val=Am9513_input_data()<<8;			\
		val|=Am9513_input_data();			\
	}while(0)

#else /* Am9513_16BITBUS */

#define Am9513_write_register(reg,val)				\
	do{							\
		Am9513_output_control(reg);			\
		Am9513_output_data(val);			\
	}while(0)

#define Am9513_read_register(reg,val)				\
	do{							\
		Am9513_output_control(reg);			\
		val=Am9513_input_data();			\
	}while(0)

#endif

#endif
