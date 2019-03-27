/* GNU DBM - DataBase Manager include file
   Copyright 1989, 1991  Free Software Foundation, Inc.
   Written by Philip A. Nelson.

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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* You may contact the author by:
       e-mail:  phil@wwu.edu
      us-mail:  Philip A. Nelson
                Computer Science Department
                Western Washington University
                Bellingham, WA 98226
        phone:  (206) 676-3035
       
*************************************************************************/

/* Parameters to gdbm_open for READERS, WRITERS, and WRITERS who
   can create the database. */
#define  GDBM_READER  0
#define  GDBM_WRITER  1
#define  GDBM_WRCREAT 2
#define  GDBM_NEWDB   3

/* Parameters to gdbm_store for simple insertion or replacement. */
#define  GDBM_INSERT  0
#define  GDBM_REPLACE 1


/* The data and key structure.  This structure is defined for compatibility. */
typedef struct {
	char *dptr;
	int   dsize;
      } datum;


/* The file information header. This is good enough for most applications. */
typedef struct {int dummy[10];} *GDBM_FILE;


/* These are the routines! */

extern GDBM_FILE gdbm_open ();

extern void	 gdbm_close ();

extern datum	 gdbm_fetch ();

extern int	 gdbm_store ();

extern int	 gdbm_delete ();

extern datum	 gdbm_firstkey ();

extern datum	 gdbm_nextkey ();

extern int	 gdbm_reorganize ();


/* gdbm sends back the following error codes in the variable gdbm_errno. */
typedef enum {	NO_ERROR,
		MALLOC_ERROR,
		BLOCK_SIZE_ERROR,
		FILE_OPEN_ERROR,
		FILE_WRITE_ERROR,
		FILE_SEEK_ERROR,
		FILE_READ_ERROR,
		BAD_MAGIC_NUMBER,
		EMPTY_DATABASE,
		CANT_BE_READER,
	        CANT_BE_WRITER,
		READER_CANT_RECOVER,
		READER_CANT_DELETE,
		READER_CANT_STORE,
		READER_CANT_REORGANIZE,
		UNKNOWN_UPDATE,
		ITEM_NOT_FOUND,
		REORGANIZE_FAILED,
		CANNOT_REPLACE}
	gdbm_error;
