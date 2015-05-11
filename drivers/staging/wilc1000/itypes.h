/*****************************************************************************/
/*                                                                           */
/*                     Ittiam 802.11 MAC SOFTWARE                            */
/*                                                                           */
/*                  ITTIAM SYSTEMS PVT LTD, BANGALORE                        */
/*                           COPYRIGHT(C) 2005                               */
/*                                                                           */
/*  This program  is  proprietary to  Ittiam  Systems  Private  Limited  and */
/*  is protected under Indian  Copyright Law as an unpublished work. Its use */
/*  and  disclosure  is  limited by  the terms  and  conditions of a license */
/*  agreement. It may not be copied or otherwise  reproduced or disclosed to */
/*  persons outside the licensee's organization except in accordance with the*/
/*  terms  and  conditions   of  such  an  agreement.  All  copies  and      */
/*  reproductions shall be the property of Ittiam Systems Private Limited and*/
/*  must bear this notice in its entirety.                                   */
/*                                                                           */
/*****************************************************************************/

/*****************************************************************************/
/*                                                                           */
/*  File Name         : itypes.h                                             */
/*                                                                           */
/*  Description       : This file contains all the data type definitions for */
/*                      MAC implementation.                                  */
/*                                                                           */
/*  List of Functions : None                                                 */
/*  Issues / Problems : None                                                 */
/*                                                                           */
/*  Revision History  :                                                      */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes                              */
/*         01 05 2005   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

#ifndef ITYPES_H
#define ITYPES_H


/*****************************************************************************/
/* Data Types                                                                */
/*****************************************************************************/

typedef int WORD32;
typedef short WORD16;
typedef char WORD8;
typedef unsigned int UWORD32;
typedef unsigned short UWORD16;
typedef unsigned char UWORD8;

/*****************************************************************************/
/* Enums                                                                     */
/*****************************************************************************/

typedef enum {
	BFALSE = 0,
	BTRUE  = 1
} BOOL_T;

#endif /* ITYPES_H */
