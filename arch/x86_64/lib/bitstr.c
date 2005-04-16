#include <linux/module.h>
#include <linux/bitops.h>

/* Find string of zero bits in a bitmap */ 
unsigned long 
find_next_zero_string(unsigned long *bitmap, long start, long nbits, int len)
{ 
	unsigned long n, end, i; 	

 again:
	n = find_next_zero_bit(bitmap, nbits, start);
	if (n == -1) 
		return -1;
	
	/* could test bitsliced, but it's hardly worth it */
	end = n+len;
	if (end >= nbits) 
		return -1; 
	for (i = n+1; i < end; i++) { 
		if (test_bit(i, bitmap)) {  
			start = i+1; 
			goto again; 
		} 
	}
	return n;
}

EXPORT_SYMBOL(find_next_zero_string);
