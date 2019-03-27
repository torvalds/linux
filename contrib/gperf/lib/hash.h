// This may look like C code, but it is really -*- C++ -*-

/* 
Copyright (C) 1988, 1992, 2000, 2002 Free Software Foundation
    written by Doug Lea <dl@rocky.oswego.edu>
*/

#ifndef _hash_h
#define _hash_h 1

/* a hash function for char[] arrays using the
   method described in Aho, Sethi, & Ullman, p 436. */
extern unsigned int hashpjw (const unsigned char *string, unsigned int len);

#endif
