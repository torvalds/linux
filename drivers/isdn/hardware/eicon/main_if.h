/*
 *
  Copyright (c) Eicon Technology Corporation, 2000.
 *
  This source file is supplied for the use with Eicon
  Technology Corporation's range of DIVA Server Adapters.
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*------------------------------------------------------------------*/
/* file: main_if.h                                                  */
/*------------------------------------------------------------------*/
# ifndef MAIN_IF___H
# define MAIN_IF___H

# include "debug_if.h"

void  DI_lock (void) ;
void  DI_unlock (void) ;

#ifdef NOT_YET_NEEDED
void  DI_nttime (LARGE_INTEGER *NTtime) ;
void  DI_ntlcltime(LARGE_INTEGER *NTtime, LARGE_INTEGER *lclNTtime) ;
void  DI_nttimefields(LARGE_INTEGER *NTtime, TIME_FIELDS *TimeFields);
unsigned long  DI_wintime(LARGE_INTEGER *NTtime) ;

unsigned short  DiInsertProcessorNumber (int type) ;
void DiProcessEventLog (unsigned short id, unsigned long msgID, va_list ap);

void  StartIoctlTimer (void (*Handler)(void), unsigned long msec) ;
void  StopIoctlTimer (void) ;
void  UnpendIoctl (DbgRequest *pDbgReq) ;
#endif

void add_to_q(int, char* , unsigned int);
# endif /* MAIN_IF___H */

