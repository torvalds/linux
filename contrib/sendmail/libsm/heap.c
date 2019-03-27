/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: heap.c,v 1.52 2013-11-22 20:51:43 ca Exp $")

/*
**  debugging memory allocation package
**  See heap.html for documentation.
*/

#include <string.h>

#include <sm/assert.h>
#include <sm/debug.h>
#include <sm/exc.h>
#include <sm/heap.h>
#include <sm/io.h>
#include <sm/signal.h>
#include <sm/xtrap.h>

/* undef all macro versions of the "functions" so they can be specified here */
#undef sm_malloc
#undef sm_malloc_x
#undef sm_malloc_tagged
#undef sm_malloc_tagged_x
#undef sm_free
#undef sm_free_tagged
#undef sm_realloc
#if SM_HEAP_CHECK
# undef sm_heap_register
# undef sm_heap_checkptr
# undef sm_heap_report
#endif /* SM_HEAP_CHECK */

#if SM_HEAP_CHECK
SM_DEBUG_T SmHeapCheck = SM_DEBUG_INITIALIZER("sm_check_heap",
    "@(#)$Debug: sm_check_heap - check sm_malloc, sm_realloc, sm_free calls $");
# define HEAP_CHECK sm_debug_active(&SmHeapCheck, 1)
static int	ptrhash __P((void *p));
#endif /* SM_HEAP_CHECK */

const SM_EXC_TYPE_T SmHeapOutOfMemoryType =
{
	SmExcTypeMagic,
	"F:sm.heap",
	"",
	sm_etype_printf,
	"out of memory",
};

SM_EXC_T SmHeapOutOfMemory = SM_EXC_INITIALIZER(&SmHeapOutOfMemoryType, NULL);


/*
**  The behaviour of malloc with size==0 is platform dependent (it
**  says so in the C standard): it can return NULL or non-NULL.  We
**  don't want sm_malloc_x(0) to raise an exception on some platforms
**  but not others, so this case requires special handling.  We've got
**  two choices: "size = 1" or "return NULL". We use the former in the
**  following.
**	If we had something like autoconf we could figure out the
**	behaviour of the platform and either use this hack or just
**	use size.
*/

#define MALLOC_SIZE(size)	((size) == 0 ? 1 : (size))

/*
**  SM_MALLOC_X -- wrapper around malloc(), raises an exception on error.
**
**	Parameters:
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to memory region.
**
**	Note:
**		sm_malloc_x only gets called from source files in which heap
**		debugging is disabled at compile time.  Otherwise, a call to
**		sm_malloc_x is macro expanded to a call to sm_malloc_tagged_x.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

void *
sm_malloc_x(size)
	size_t size;
{
	void *ptr;

	ENTER_CRITICAL();
	ptr = malloc(MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	if (ptr == NULL)
		sm_exc_raise_x(&SmHeapOutOfMemory);
	return ptr;
}

#if !SM_HEAP_CHECK

/*
**  SM_MALLOC -- wrapper around malloc()
**
**	Parameters:
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to memory region.
*/

void *
sm_malloc(size)
	size_t size;
{
	void *ptr;

	ENTER_CRITICAL();
	ptr = malloc(MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	return ptr;
}

/*
**  SM_REALLOC -- wrapper for realloc()
**
**	Parameters:
**		ptr -- pointer to old memory area.
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to new memory area, NULL on failure.
*/

void *
sm_realloc(ptr, size)
	void *ptr;
	size_t size;
{
	void *newptr;

	ENTER_CRITICAL();
	newptr = realloc(ptr, MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	return newptr;
}

/*
**  SM_REALLOC_X -- wrapper for realloc()
**
**	Parameters:
**		ptr -- pointer to old memory area.
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to new memory area.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

void *
sm_realloc_x(ptr, size)
	void *ptr;
	size_t size;
{
	void *newptr;

	ENTER_CRITICAL();
	newptr = realloc(ptr, MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	if (newptr == NULL)
		sm_exc_raise_x(&SmHeapOutOfMemory);
	return newptr;
}
/*
**  SM_FREE -- wrapper around free()
**
**	Parameters:
**		ptr -- pointer to memory region.
**
**	Returns:
**		none.
*/

void
sm_free(ptr)
	void *ptr;
{
	if (ptr == NULL)
		return;
	ENTER_CRITICAL();
	free(ptr);
	LEAVE_CRITICAL();
	return;
}

#else /* !SM_HEAP_CHECK */

/*
**  Each allocated block is assigned a "group number".
**  By default, all blocks are assigned to group #1.
**  By convention, group #0 is for memory that is never freed.
**  You can use group numbers any way you want, in order to help make
**  sense of sm_heap_report output.
*/

int SmHeapGroup = 1;
int SmHeapMaxGroup = 1;

/*
**  Total number of bytes allocated.
**  This is only maintained if the sm_check_heap debug category is active.
*/

size_t SmHeapTotal = 0;

/*
**  High water mark: the most that SmHeapTotal has ever been.
*/

size_t SmHeapMaxTotal = 0;

/*
**  Maximum number of bytes that may be allocated at any one time.
**  0 means no limit.
**  This is only honoured if sm_check_heap is active.
*/

SM_DEBUG_T SmHeapLimit = SM_DEBUG_INITIALIZER("sm_heap_limit",
    "@(#)$Debug: sm_heap_limit - max # of bytes permitted in heap $");

/*
**  This is the data structure that keeps track of all currently
**  allocated blocks of memory known to the heap package.
*/

typedef struct sm_heap_item SM_HEAP_ITEM_T;
struct sm_heap_item
{
	void		*hi_ptr;
	size_t		hi_size;
	char		*hi_tag;
	int		hi_num;
	int		hi_group;
	SM_HEAP_ITEM_T	*hi_next;
};

#define SM_HEAP_TABLE_SIZE	256
static SM_HEAP_ITEM_T *SmHeapTable[SM_HEAP_TABLE_SIZE];

/*
**  This is a randomly generated table
**  which contains exactly one occurrence
**  of each of the numbers between 0 and 255.
**  It is used by ptrhash.
*/

static unsigned char hashtab[SM_HEAP_TABLE_SIZE] =
{
	161, 71, 77,187, 15,229,  9,176,221,119,239, 21, 85,138,203, 86,
	102, 65, 80,199,235, 32,140, 96,224, 78,126,127,144,  0, 11,179,
	 64, 30,120, 23,225,226, 33, 50,205,167,130,240,174, 99,206, 73,
	231,210,189,162, 48, 93,246, 54,213,141,135, 39, 41,192,236,193,
	157, 88, 95,104,188, 63,133,177,234,110,158,214,238,131,233, 91,
	125, 82, 94, 79, 66, 92,151, 45,252, 98, 26,183,  7,191,171,106,
	145,154,251,100,113,  5, 74, 62, 76,124, 14,217,200, 75,115,190,
	103, 28,198,196,169,219, 37,118,150, 18,152,175, 49,136,  6,142,
	 89, 19,243,254, 47,137, 24,166,180, 10, 40,186,202, 46,184, 67,
	148,108,181, 81, 25,241, 13,139, 58, 38, 84,253,201, 12,116, 17,
	195, 22,112, 69,255, 43,147,222,111, 56,194,216,149,244, 42,173,
	232,220,249,105,207, 51,197,242, 72,211,208, 59,122,230,237,170,
	165, 44, 68,123,129,245,143,101,  8,209,215,247,185, 57,218, 53,
	114,121,  3,128,  4,204,212,146,  2,155, 83,250, 87, 29, 31,159,
	 60, 27,107,156,227,182,  1, 61, 36,160,109, 97, 90, 20,168,132,
	223,248, 70,164, 55,172, 34, 52,163,117, 35,153,134, 16,178,228
};

/*
**  PTRHASH -- hash a pointer value
**
**	Parameters:
**		p -- pointer.
**
**	Returns:
**		hash value.
**
**  ptrhash hashes a pointer value to a uniformly distributed random
**  number between 0 and 255.
**
**  This hash algorithm is based on Peter K. Pearson,
**  "Fast Hashing of Variable-Length Text Strings",
**  in Communications of the ACM, June 1990, vol 33 no 6.
*/

static int
ptrhash(p)
	void *p;
{
	int h;

	if (sizeof(void*) == 4 && sizeof(unsigned long) == 4)
	{
		unsigned long n = (unsigned long)p;

		h = hashtab[n & 0xFF];
		h = hashtab[h ^ ((n >> 8) & 0xFF)];
		h = hashtab[h ^ ((n >> 16) & 0xFF)];
		h = hashtab[h ^ ((n >> 24) & 0xFF)];
	}
# if 0
	else if (sizeof(void*) == 8 && sizeof(unsigned long) == 8)
	{
		unsigned long n = (unsigned long)p;

		h = hashtab[n & 0xFF];
		h = hashtab[h ^ ((n >> 8) & 0xFF)];
		h = hashtab[h ^ ((n >> 16) & 0xFF)];
		h = hashtab[h ^ ((n >> 24) & 0xFF)];
		h = hashtab[h ^ ((n >> 32) & 0xFF)];
		h = hashtab[h ^ ((n >> 40) & 0xFF)];
		h = hashtab[h ^ ((n >> 48) & 0xFF)];
		h = hashtab[h ^ ((n >> 56) & 0xFF)];
	}
# endif /* 0 */
	else
	{
		unsigned char *cp = (unsigned char *)&p;
		int i;

		h = 0;
		for (i = 0; i < sizeof(void*); ++i)
			h = hashtab[h ^ cp[i]];
	}
	return h;
}

/*
**  SM_MALLOC_TAGGED -- wrapper around malloc(), debugging version.
**
**	Parameters:
**		size -- size of requested memory.
**		tag -- tag for debugging.
**		num -- additional value for debugging.
**		group -- heap group for debugging.
**
**	Returns:
**		Pointer to memory region.
*/

void *
sm_malloc_tagged(size, tag, num, group)
	size_t size;
	char *tag;
	int num;
	int group;
{
	void *ptr;

	if (!HEAP_CHECK)
	{
		ENTER_CRITICAL();
		ptr = malloc(MALLOC_SIZE(size));
		LEAVE_CRITICAL();
		return ptr;
	}

	if (sm_xtrap_check())
		return NULL;
	if (sm_debug_active(&SmHeapLimit, 1)
	    && sm_debug_level(&SmHeapLimit) < SmHeapTotal + size)
		return NULL;
	ENTER_CRITICAL();
	ptr = malloc(MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	if (ptr != NULL && !sm_heap_register(ptr, size, tag, num, group))
	{
		ENTER_CRITICAL();
		free(ptr);
		LEAVE_CRITICAL();
		ptr = NULL;
	}
	SmHeapTotal += size;
	if (SmHeapTotal > SmHeapMaxTotal)
		SmHeapMaxTotal = SmHeapTotal;
	return ptr;
}

/*
**  SM_MALLOC_TAGGED_X -- wrapper around malloc(), debugging version.
**
**	Parameters:
**		size -- size of requested memory.
**		tag -- tag for debugging.
**		num -- additional value for debugging.
**		group -- heap group for debugging.
**
**	Returns:
**		Pointer to memory region.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

void *
sm_malloc_tagged_x(size, tag, num, group)
	size_t size;
	char *tag;
	int num;
	int group;
{
	void *ptr;

	if (!HEAP_CHECK)
	{
		ENTER_CRITICAL();
		ptr = malloc(MALLOC_SIZE(size));
		LEAVE_CRITICAL();
		if (ptr == NULL)
			sm_exc_raise_x(&SmHeapOutOfMemory);
		return ptr;
	}

	sm_xtrap_raise_x(&SmHeapOutOfMemory);
	if (sm_debug_active(&SmHeapLimit, 1)
	    && sm_debug_level(&SmHeapLimit) < SmHeapTotal + size)
	{
		sm_exc_raise_x(&SmHeapOutOfMemory);
	}
	ENTER_CRITICAL();
	ptr = malloc(MALLOC_SIZE(size));
	LEAVE_CRITICAL();
	if (ptr != NULL && !sm_heap_register(ptr, size, tag, num, group))
	{
		ENTER_CRITICAL();
		free(ptr);
		LEAVE_CRITICAL();
		ptr = NULL;
	}
	if (ptr == NULL)
		sm_exc_raise_x(&SmHeapOutOfMemory);
	SmHeapTotal += size;
	if (SmHeapTotal > SmHeapMaxTotal)
		SmHeapMaxTotal = SmHeapTotal;
	return ptr;
}

/*
**  SM_HEAP_REGISTER -- register a pointer into the heap for debugging.
**
**	Parameters:
**		ptr -- pointer to register.
**		size -- size of requested memory.
**		tag -- tag for debugging.
**		num -- additional value for debugging.
**		group -- heap group for debugging.
**
**	Returns:
**		true iff successfully registered (not yet in table).
*/

bool
sm_heap_register(ptr, size, tag, num, group)
	void *ptr;
	size_t size;
	char *tag;
	int num;
	int group;
{
	int i;
	SM_HEAP_ITEM_T *hi;

	if (!HEAP_CHECK)
		return true;
	SM_REQUIRE(ptr != NULL);
	i = ptrhash(ptr);
# if SM_CHECK_REQUIRE

	/*
	** We require that ptr is not already in SmHeapTable.
	*/

	for (hi = SmHeapTable[i]; hi != NULL; hi = hi->hi_next)
	{
		if (hi->hi_ptr == ptr)
			sm_abort("sm_heap_register: ptr %p is already registered (%s:%d)",
				 ptr, hi->hi_tag, hi->hi_num);
	}
# endif /* SM_CHECK_REQUIRE */
	ENTER_CRITICAL();
	hi = (SM_HEAP_ITEM_T *) malloc(sizeof(SM_HEAP_ITEM_T));
	LEAVE_CRITICAL();
	if (hi == NULL)
		return false;
	hi->hi_ptr = ptr;
	hi->hi_size = size;
	hi->hi_tag = tag;
	hi->hi_num = num;
	hi->hi_group = group;
	hi->hi_next = SmHeapTable[i];
	SmHeapTable[i] = hi;
	return true;
}
/*
**  SM_REALLOC -- wrapper for realloc(), debugging version.
**
**	Parameters:
**		ptr -- pointer to old memory area.
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to new memory area, NULL on failure.
*/

void *
sm_realloc(ptr, size)
	void *ptr;
	size_t size;
{
	void *newptr;
	SM_HEAP_ITEM_T *hi, **hp;

	if (!HEAP_CHECK)
	{
		ENTER_CRITICAL();
		newptr = realloc(ptr, MALLOC_SIZE(size));
		LEAVE_CRITICAL();
		return newptr;
	}

	if (ptr == NULL)
		return sm_malloc_tagged(size, "realloc", 0, SmHeapGroup);

	for (hp = &SmHeapTable[ptrhash(ptr)]; *hp != NULL; hp = &(**hp).hi_next)
	{
		if ((**hp).hi_ptr == ptr)
		{
			if (sm_xtrap_check())
				return NULL;
			hi = *hp;
			if (sm_debug_active(&SmHeapLimit, 1)
			    && sm_debug_level(&SmHeapLimit)
			       < SmHeapTotal - hi->hi_size + size)
			{
				return NULL;
			}
			ENTER_CRITICAL();
			newptr = realloc(ptr, MALLOC_SIZE(size));
			LEAVE_CRITICAL();
			if (newptr == NULL)
				return NULL;
			SmHeapTotal = SmHeapTotal - hi->hi_size + size;
			if (SmHeapTotal > SmHeapMaxTotal)
				SmHeapMaxTotal = SmHeapTotal;
			*hp = hi->hi_next;
			hi->hi_ptr = newptr;
			hi->hi_size = size;
			hp = &SmHeapTable[ptrhash(newptr)];
			hi->hi_next = *hp;
			*hp = hi;
			return newptr;
		}
	}
	sm_abort("sm_realloc: bad argument (%p)", ptr);
	/* NOTREACHED */
	return NULL;	/* keep Irix compiler happy */
}

/*
**  SM_REALLOC_X -- wrapper for realloc(), debugging version.
**
**	Parameters:
**		ptr -- pointer to old memory area.
**		size -- size of requested memory.
**
**	Returns:
**		Pointer to new memory area.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

void *
sm_realloc_x(ptr, size)
	void *ptr;
	size_t size;
{
	void *newptr;
	SM_HEAP_ITEM_T *hi, **hp;

	if (!HEAP_CHECK)
	{
		ENTER_CRITICAL();
		newptr = realloc(ptr, MALLOC_SIZE(size));
		LEAVE_CRITICAL();
		if (newptr == NULL)
			sm_exc_raise_x(&SmHeapOutOfMemory);
		return newptr;
	}

	if (ptr == NULL)
		return sm_malloc_tagged_x(size, "realloc", 0, SmHeapGroup);

	for (hp = &SmHeapTable[ptrhash(ptr)]; *hp != NULL; hp = &(**hp).hi_next)
	{
		if ((**hp).hi_ptr == ptr)
		{
			sm_xtrap_raise_x(&SmHeapOutOfMemory);
			hi = *hp;
			if (sm_debug_active(&SmHeapLimit, 1)
			    && sm_debug_level(&SmHeapLimit)
			       < SmHeapTotal - hi->hi_size + size)
			{
				sm_exc_raise_x(&SmHeapOutOfMemory);
			}
			ENTER_CRITICAL();
			newptr = realloc(ptr, MALLOC_SIZE(size));
			LEAVE_CRITICAL();
			if (newptr == NULL)
				sm_exc_raise_x(&SmHeapOutOfMemory);
			SmHeapTotal = SmHeapTotal - hi->hi_size + size;
			if (SmHeapTotal > SmHeapMaxTotal)
				SmHeapMaxTotal = SmHeapTotal;
			*hp = hi->hi_next;
			hi->hi_ptr = newptr;
			hi->hi_size = size;
			hp = &SmHeapTable[ptrhash(newptr)];
			hi->hi_next = *hp;
			*hp = hi;
			return newptr;
		}
	}
	sm_abort("sm_realloc_x: bad argument (%p)", ptr);
	/* NOTREACHED */
	return NULL;	/* keep Irix compiler happy */
}

/*
**  SM_FREE_TAGGED -- wrapper around free(), debugging version.
**
**	Parameters:
**		ptr -- pointer to memory region.
**		tag -- tag for debugging.
**		num -- additional value for debugging.
**
**	Returns:
**		none.
*/

void
sm_free_tagged(ptr, tag, num)
	void *ptr;
	char *tag;
	int num;
{
	SM_HEAP_ITEM_T **hp;

	if (ptr == NULL)
		return;
	if (!HEAP_CHECK)
	{
		ENTER_CRITICAL();
		free(ptr);
		LEAVE_CRITICAL();
		return;
	}
	for (hp = &SmHeapTable[ptrhash(ptr)]; *hp != NULL; hp = &(**hp).hi_next)
	{
		if ((**hp).hi_ptr == ptr)
		{
			SM_HEAP_ITEM_T *hi = *hp;

			*hp = hi->hi_next;

			/*
			**  Fill the block with zeros before freeing.
			**  This is intended to catch problems with
			**  dangling pointers.  The block is filled with
			**  zeros, not with some non-zero value, because
			**  it is common practice in some C code to store
			**  a zero in a structure member before freeing the
			**  structure, as a defense against dangling pointers.
			*/

			(void) memset(ptr, 0, hi->hi_size);
			SmHeapTotal -= hi->hi_size;
			ENTER_CRITICAL();
			free(ptr);
			free(hi);
			LEAVE_CRITICAL();
			return;
		}
	}
	sm_abort("sm_free: bad argument (%p) (%s:%d)", ptr, tag, num);
}

/*
**  SM_HEAP_CHECKPTR_TAGGED -- check whether ptr is a valid argument to sm_free
**
**	Parameters:
**		ptr -- pointer to memory region.
**		tag -- tag for debugging.
**		num -- additional value for debugging.
**
**	Returns:
**		none.
**
**	Side Effects:
**		aborts if check fails.
*/

void
sm_heap_checkptr_tagged(ptr, tag, num)
	void *ptr;
	char *tag;
	int num;
{
	SM_HEAP_ITEM_T *hp;

	if (!HEAP_CHECK)
		return;
	if (ptr == NULL)
		return;
	for (hp = SmHeapTable[ptrhash(ptr)]; hp != NULL; hp = hp->hi_next)
	{
		if (hp->hi_ptr == ptr)
			return;
	}
	sm_abort("sm_heap_checkptr(%p): bad ptr (%s:%d)", ptr, tag, num);
}

/*
**  SM_HEAP_REPORT -- output "map" of used heap.
**
**	Parameters:
**		stream -- the file pointer to write to.
**		verbosity -- how much info?
**
**	Returns:
**		none.
*/

void
sm_heap_report(stream, verbosity)
	SM_FILE_T *stream;
	int verbosity;
{
	int i;
	unsigned long group0total, group1total, otherstotal, grandtotal;

	if (!HEAP_CHECK || verbosity <= 0)
		return;
	group0total = group1total = otherstotal = grandtotal = 0;
	for (i = 0; i < sizeof(SmHeapTable) / sizeof(SmHeapTable[0]); ++i)
	{
		SM_HEAP_ITEM_T *hi = SmHeapTable[i];

		while (hi != NULL)
		{
			if (verbosity > 2
			    || (verbosity > 1 && hi->hi_group != 0))
			{
				sm_io_fprintf(stream, SM_TIME_DEFAULT,
					"%4d %*lx %7lu bytes",
					hi->hi_group,
					(int) sizeof(void *) * 2,
					(long)hi->hi_ptr,
					(unsigned long)hi->hi_size);
				if (hi->hi_tag != NULL)
				{
					sm_io_fprintf(stream, SM_TIME_DEFAULT,
						"  %s",
						hi->hi_tag);
					if (hi->hi_num)
					{
						sm_io_fprintf(stream,
							SM_TIME_DEFAULT,
							":%d",
							hi->hi_num);
					}
				}
				sm_io_fprintf(stream, SM_TIME_DEFAULT, "\n");
			}
			switch (hi->hi_group)
			{
			  case 0:
				group0total += hi->hi_size;
				break;
			  case 1:
				group1total += hi->hi_size;
				break;
			  default:
				otherstotal += hi->hi_size;
				break;
			}
			grandtotal += hi->hi_size;
			hi = hi->hi_next;
		}
	}
	sm_io_fprintf(stream, SM_TIME_DEFAULT,
		"heap max=%lu, total=%lu, ",
		(unsigned long) SmHeapMaxTotal, grandtotal);
	sm_io_fprintf(stream, SM_TIME_DEFAULT,
		"group 0=%lu, group 1=%lu, others=%lu\n",
		group0total, group1total, otherstotal);
	if (grandtotal != SmHeapTotal)
	{
		sm_io_fprintf(stream, SM_TIME_DEFAULT,
			"BUG => SmHeapTotal: got %lu, expected %lu\n",
			(unsigned long) SmHeapTotal, grandtotal);
	}
}
#endif /* !SM_HEAP_CHECK */
