/*
 * This file is te-generic.h and is intended to be a template for
 * target environment specific header files.
 *
 * It is my intent that this file will evolve into a file suitable for config,
 * compile, and copying as an aid for testing and porting.  xoxorich.
 */

/* Added these, because if we don't know what we're targeting we may
   need an assembler version of libgcc, and that will use local
   labels.  */
#define LOCAL_LABELS_DOLLAR 1
#define LOCAL_LABELS_FB 1

/* these define interfaces */
#ifdef OBJ_HEADER
#include OBJ_HEADER
#else
#include "obj-format.h"
#endif

/* end of te-generic.h */
