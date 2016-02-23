/*
 *	Just a place holder. 
 */

#ifndef _UAPI_SPARC_SETUP_H
#define _UAPI_SPARC_SETUP_H

#if defined(__sparc__) && defined(__arch64__)
# define COMMAND_LINE_SIZE 2048
#else
# define COMMAND_LINE_SIZE 256
#endif


#endif /* _UAPI_SPARC_SETUP_H */
