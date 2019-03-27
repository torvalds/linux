/* Macros for the 'type' part of an fopen, freopen or fdopen. 

	<Read|Write>[Update]<Binary file|text file>

   This version is for "binary" systems, where text and binary files are
   different.  An example is Mess-Dose.  Many Unix systems could also
   cope with a "b" in the string, indicating binary files, but some reject this
   (and thereby don't conform to ANSI C, but what else is new?).

   This file is designed for inclusion by host-dependent .h files.  No
   user application should include it directly, since that would make
   the application unable to be configured for both "same" and "binary"
   variant systems.  */

#define FOPEN_RB	"rb"
#define FOPEN_WB 	"wb"
#define FOPEN_AB 	"ab"
#define FOPEN_RUB 	"r+b"
#define FOPEN_WUB 	"w+b"
#define FOPEN_AUB 	"a+b"

#define FOPEN_RT	"r"
#define FOPEN_WT 	"w"
#define FOPEN_AT 	"a"
#define FOPEN_RUT 	"r+"
#define FOPEN_WUT 	"w+"
#define FOPEN_AUT 	"a+"
