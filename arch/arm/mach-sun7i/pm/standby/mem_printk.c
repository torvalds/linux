/**
 * common.c - common operations
 * date:    2012-2-13 8:42:56
 * author:  Aaron<leafy.myeh@allwinnertech.com>
 * history: V0.1
 */

#include <stdarg.h>
#include "pm_types.h"
#include "pm.h"
#include "mem_printk.h"

#define NUM_TYPE long long

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* Must be 32 == 0x20 */
#define SPECIAL	64		/* 0x */
#define is_digit(c)	((c) >= '0' && (c) <= '9')


/* Basic string functions */

/*
  s t r l e n

  returns number of characters in s (not including terminating null character)
*/
size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
	{
		/* nothing */
		;
	}
	return sc - s;
}

/*
  s t r c p y

  Copy 'src' to 'dest'. Strings may not overlap.
*/
char *strcpy(char *dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0')
	{
		/* nothing */
		;
	}
	return tmp;
}

char *strncpy(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	while (count)
	{
		if ((*tmp = *src) != 0)
		{
			src++;
		}
		tmp++;
		count--;
	}
	return dest;
}


char *strcat(char *dest, const char *src)
{
	char *tmp = dest;

	while (*dest)
	{
		dest++;
	}
	while ((*dest++ = *src++) != '\0')
	{
		;
	}
	return tmp;
}


char *strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count)
	{
		while (*dest)
		{
			dest++;
		}
		while ((*dest++ = *src++) != 0)
		{
			if (--count == 0)
			{
				*dest = '\0';
				break;
			}
		}
	}
	return tmp;
}

int strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1)
	{
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
		{
			return c1 < c2 ? -1 : 1;
		}
		if (!c1)
		{
			break;
		}
	}
	return 0;
}


int strncmp(const char *cs, const char *ct, size_t count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}
	return 0;
}

static int skip_atoi(const char **s)
{
	int i=0;

	while (is_digit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}

char *itoa(int value, char *string, int radix)
{
	char stack[16];
	int  negative = 0;			//defualt is positive value
	int  i;
	int  j;
	char digit_string[] = "0123456789ABCDEF";	
	
	if(value == 0)
	{
		//zero
		string[0] = '0';
		string[1] = '\0';
		return string;
	}
	
	if(value < 0)
	{
		//'value' is negative, convert to postive first
		negative = 1;
		value = -value ;
	}
	
	for(i = 0; value > 0; ++i)
	{
		// characters in reverse order are put in 'stack'.
		stack[i] = digit_string[value % radix];
		value /= radix;
	}
	
	//restore reversed order result to user string
    j = 0;
	if(negative)
	{
		//add sign at first charset.
		string[j++] = '-';
	}
	for(--i; i >= 0; --i, ++j)
	{
		string[j] = stack[i];
	}
	//must end with '\0'.
	string[j] = '\0';
	
	return string;
}

char *utoa(unsigned int value, char *string, int radix)
{
	char stack[16];
	int  i;
	int  j;
	char digit_string[] = "0123456789ABCDEF";	
	
	if(value == 0)
	{
		//zero
		string[0] = '0';
		string[1] = '\0';
		return string;
	}
	
	for(i = 0; value > 0; ++i)
	{
		// characters in reverse order are put in 'stack'.
		stack[i] = digit_string[value % radix];
		value /= radix;
	}
	
	//restore reversed order result to user string
    for(--i, j = 0; i >= 0; --i, ++j)
	{
		string[j] = stack[i];
	}
	//must end with '\0'.
	string[j] = '\0';
	
	return string;
}

/*
*********************************************************************************************************
*                                       	FORMATTED PRINTF
*
* Description: 	print out a formatted string, similar to ANSI-C function printf().
*				This function can support and only support the following conversion specifiers:
*              	%d	signed decimal integer.
*              	%u	unsigned decimal integer.
*              	%x	unsigned hexadecimal integer, using hex digits 0x.
*              	%c	single character.
*              	%s	character string.
*
* Arguments  : 	format	: format control.
*				...		: arguments.
*
* Returns    : 	the number of characters printed out.
*
* Note		 : 	the usage refer to ANSI-C function printf().
*********************************************************************************************************
*/
char debugger_buffer[DEBUG_BUFFER_SIZE];
__s32 printk(const char *format, ...)
{
	va_list args;
	char 	string[16];	//align by cpu word
	char 	*pdest;
	char 	*psrc;
	__s32 	align;
	__s32		len = 0;
	
	//dump current timestemp
	//print_current_time();
	
	pdest = debugger_buffer;
	va_start(args, format);
	while(*format)
	{
		if(*format == '%')
		{
			++format;
			if (('0' < (*format)) && ((*format) <= '9'))
			{
				//we just suport wide from 1 to 9.
				align = *format - '0';
				++format;
			}
			else
			{
				align = 0;
			}
			switch(*format)
			{
				case 'd':
				{
					//int
					itoa(va_arg(args, int), string, 10);
                    len = strlen(string);
                    len += print_align(string, len, align);
                    strcpy(pdest, string);
                    pdest += len;
                    break;
				}
				case 'x': 
				case 'p':
				{
					//hex
					utoa(va_arg(args, int), string, 16);
					len = strlen(string);
					len += print_align(string, len, align);
					strcpy(pdest, string);
                    pdest += len;
                    break;
				}
				case 'u': 
				{
					//unsigned int
					utoa(va_arg(args, int), string, 10);
                    len = strlen(string);
                    len += print_align(string, len, align);
                    strcpy(pdest, string);
					pdest += len;
					break;
				}
				case 'c': 
				{
					//charset, aligned by cpu word
					*pdest = (char)va_arg(args, int);
					break;
				}
				case 's':
				{
					//string
					psrc = va_arg(args, char *);
					strcpy(pdest, psrc);
					pdest += strlen(psrc);
					break;
				}
				default : 
				{
					//no-conversion
					*pdest++ = '%';
					*pdest++ = *format;
				}
			}
		}
		else
		{
			*pdest++ = *format;
		}
		//parse next token
		++format;
	}
	va_end(args);
	
	//must end with '\0'
	*pdest = '\0';
	pdest++;
	serial_puts(debugger_buffer);
	
	return (pdest - debugger_buffer);
}


__s32 print_align(char *string, __s32 len, __s32 align)
{
	//fill with space ' ' when align request,
	//the max align length is 16 byte.
	char fill_ch[] = "                ";
	if (len < align)
	{
		//fill at right
		strncat(string, fill_ch, align - len);
		return align - len;
	}
	//not fill anything
	return 0;
}

__s32 printk_nommu(const char *format, ...)
{
	va_list args;
	char 	string[16];	//align by cpu word
	char 	*pdest;
	char 	*psrc;
	__s32 	align;
	__s32		len = 0;
	
	//dump current timestemp
	//print_current_time();
	
	pdest = debugger_buffer;
	va_start(args, format);
	while(*format)
	{
		if(*format == '%')
		{
			++format;
			if (('0' < (*format)) && ((*format) <= '9'))
			{
				//we just suport wide from 1 to 9.
				align = *format - '0';
				++format;
			}
			else
			{
				align = 0;
			}
			switch(*format)
			{
				case 'd':
				{
					//int
					itoa(va_arg(args, int), string, 10);
                    len = strlen(string);
                    len += print_align(string, len, align);
                    strcpy(pdest, string);
                    pdest += len;
                    break;
				}
				case 'x': 
				case 'p':
				{
					//hex
					utoa(va_arg(args, int), string, 16);
					len = strlen(string);
					len += print_align(string, len, align);
					strcpy(pdest, string);
                    pdest += len;
                    break;
				}
				case 'u': 
				{
					//unsigned int
					utoa(va_arg(args, int), string, 10);
                    len = strlen(string);
                    len += print_align(string, len, align);
                    strcpy(pdest, string);
					pdest += len;
					break;
				}
				case 'c': 
				{
					//charset, aligned by cpu word
					*pdest = (char)va_arg(args, int);
					break;
				}
				case 's':
				{
					//string
					psrc = va_arg(args, char *);
					strcpy(pdest, psrc);
					pdest += strlen(psrc);
					break;
				}
				default : 
				{
					//no-conversion
					*pdest++ = '%';
					*pdest++ = *format;
				}
			}
		}
		else
		{
			*pdest++ = *format;
		}
		//parse next token
		++format;
	}
	va_end(args);
	
	//must end with '\0'
	*pdest = '\0';
	pdest++;
	serial_puts_nommu(debugger_buffer);
	
	return (pdest - debugger_buffer);
}

