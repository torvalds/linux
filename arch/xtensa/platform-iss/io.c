/* This file isn't really needed right now. */

#if 0

#include <asm/io.h>
#include <xtensa/simcall.h>

extern int __simc ();


char iss_serial_getc()
{
  char c;
  __simc( SYS_read, 0, &c, 1 );
  return c;
}

void iss_serial_putc( char c )
{
  __simc( SYS_write, 1, &c, 1 );
}

void iss_serial_puts( char *s )
{
  if( s != 0 && *s != 0 )
    __simc( SYS_write, 1, s, strlen(s) );
}

/*#error Need I/O ports to specific hardware!*/

#endif

