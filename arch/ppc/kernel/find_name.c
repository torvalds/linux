#include <stdio.h>
#include <asm/page.h>
#include <sys/mman.h>
#include <strings.h>
/*
 * Finds a given address in the System.map and prints it out
 * with its neighbors.  -- Cort
 */

int main(int argc, char **argv)
{
	unsigned long addr, cmp, i;
	FILE *f;
	char s[256], last[256];
	
	if ( argc < 2 )
	{
		fprintf(stderr, "Usage: %s <address>\n", argv[0]);
		return -1;
	}

	for ( i = 1 ; argv[i] ; i++ )
	{
		sscanf( argv[i], "%0lx", &addr );
		/* adjust if addr is relative to kernelbase */
		if ( addr < PAGE_OFFSET )
			addr += PAGE_OFFSET;
		
		if ( (f = fopen( "System.map", "r" )) == NULL )
		{
			perror("fopen()\n");
			exit(-1);
		}
		
		while ( !feof(f) )
		{
			fgets(s, 255 , f);
			sscanf( s, "%0lx", &cmp );
			if ( addr < cmp )
				break;
			strcpy( last, s);
		}
		
		printf( "%s%s", last, s );
	}		
	fclose(f);
	return 0;
}
