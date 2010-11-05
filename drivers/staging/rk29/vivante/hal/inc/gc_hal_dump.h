/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_dump_h_
#define __gc_hal_dump_h_

#ifdef __cplusplus
extern "C" {
#endif

/*
**	FILE LAYOUT:
**
**		gcsDUMP_FILE structure
**
**		gcsDUMP_DATA frame
**			gcsDUMP_DATA or gcDUMP_DATA_SIZE records rendingring the frame
**			gctUINT8 data[length]
*/

#define gcvDUMP_FILE_SIGNATURE		gcmCC('g','c','D','B')

typedef struct _gcsDUMP_FILE
{
	gctUINT32   		signature;	/* File signature */
	gctSIZE_T 			length;		/* Length of file */
	gctUINT32 			frames;		/* Number of frames in file */
}
gcsDUMP_FILE;

typedef enum _gceDUMP_TAG
{
	gcvTAG_SURFACE					= gcmCC('s','u','r','f'),
	gcvTAG_FRAME					= gcmCC('f','r','m',' '),
	gcvTAG_COMMAND					= gcmCC('c','m','d',' '),
	gcvTAG_INDEX					= gcmCC('i','n','d','x'),
	gcvTAG_STREAM					= gcmCC('s','t','r','m'),
	gcvTAG_TEXTURE					= gcmCC('t','e','x','t'),
	gcvTAG_RENDER_TARGET			= gcmCC('r','n','d','r'),
	gcvTAG_DEPTH					= gcmCC('z','b','u','f'),
	gcvTAG_RESOLVE					= gcmCC('r','s','l','v'),
	gcvTAG_DELETE					= gcmCC('d','e','l',' '),
}
gceDUMP_TAG;

typedef struct _gcsDUMP_SURFACE
{
	gceDUMP_TAG			type;		/* Type of record. */
	gctUINT32     		address;	/* Address of the surface. */
	gctINT16      		width;		/* Width of surface. */
	gctINT16	   		height;		/* Height of surface. */
	gceSURF_FORMAT		format;		/* Surface pixel format. */
	gctSIZE_T			length;		/* Number of bytes inside the surface. */
}
gcsDUMP_SURFACE;

typedef struct _gcsDUMP_DATA
{
	gceDUMP_TAG		 	type;		/* Type of record. */
	gctSIZE_T     		length;		/* Number of bytes of data. */
	gctUINT32     		address;	/* Address for the data. */
}
gcsDUMP_DATA;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_dump_h_ */

