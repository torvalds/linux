/*
 * util.h
 *  
 * helper function header file
 * 
 * a Net::DNS like library for C
 * 
 * (c) NLnet Labs, 2004
 * 
 * See the file LICENSE for the license
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <ldns/common.h>
#include <time.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define dprintf(X,Y) fprintf(stderr, (X), (Y))
/* #define	dprintf(X, Y)  */

#define LDNS_VERSION "1.7.0"
#define LDNS_REVISION ((1<<16)|(7<<8)|(0))

/**
 * splint static inline workaround
 */
#ifdef S_SPLINT_S
#  define INLINE 
#else
#  ifdef SWIG
#    define INLINE static
#  else
#    define INLINE static inline
#  endif
#endif

/**
 * Memory management macros
 */
#define LDNS_MALLOC(type)		LDNS_XMALLOC(type, 1)

#define LDNS_XMALLOC(type, count)	((type *) malloc((count) * sizeof(type)))

#define LDNS_CALLOC(type, count)	((type *) calloc((count), sizeof(type)))

#define LDNS_REALLOC(ptr, type)		LDNS_XREALLOC((ptr), type, 1)

#define LDNS_XREALLOC(ptr, type, count)				\
	((type *) realloc((ptr), (count) * sizeof(type)))

#define LDNS_FREE(ptr) \
	do { free((ptr)); (ptr) = NULL; } while (0)

#define LDNS_DEP     printf("DEPRECATED FUNCTION!\n");

/*
 * Copy data allowing for unaligned accesses in network byte order
 * (big endian).
 */
INLINE uint16_t
ldns_read_uint16(const void *src)
{
#ifdef ALLOW_UNALIGNED_ACCESSES
	return ntohs(*(const uint16_t *) src);
#else
	const uint8_t *p = (const uint8_t *) src;
	return ((uint16_t) p[0] << 8) | (uint16_t) p[1];
#endif
}

INLINE uint32_t
ldns_read_uint32(const void *src)
{
#ifdef ALLOW_UNALIGNED_ACCESSES
	return ntohl(*(const uint32_t *) src);
#else
	const uint8_t *p = (const uint8_t *) src;
	return (  ((uint32_t) p[0] << 24)
		| ((uint32_t) p[1] << 16)
		| ((uint32_t) p[2] << 8)
		|  (uint32_t) p[3]);
#endif
}

/*
 * Copy data allowing for unaligned accesses in network byte order
 * (big endian).
 */
INLINE void
ldns_write_uint16(void *dst, uint16_t data)
{
#ifdef ALLOW_UNALIGNED_ACCESSES
	* (uint16_t *) dst = htons(data);
#else
	uint8_t *p = (uint8_t *) dst;
	p[0] = (uint8_t) ((data >> 8) & 0xff);
	p[1] = (uint8_t) (data & 0xff);
#endif
}

INLINE void
ldns_write_uint32(void *dst, uint32_t data)
{
#ifdef ALLOW_UNALIGNED_ACCESSES
	* (uint32_t *) dst = htonl(data);
#else
	uint8_t *p = (uint8_t *) dst;
	p[0] = (uint8_t) ((data >> 24) & 0xff);
	p[1] = (uint8_t) ((data >> 16) & 0xff);
	p[2] = (uint8_t) ((data >> 8) & 0xff);
	p[3] = (uint8_t) (data & 0xff);
#endif
}

/* warning. */
INLINE void
ldns_write_uint64_as_uint48(void *dst, uint64_t data)
{
	uint8_t *p = (uint8_t *) dst;
	p[0] = (uint8_t) ((data >> 40) & 0xff);
	p[1] = (uint8_t) ((data >> 32) & 0xff);
	p[2] = (uint8_t) ((data >> 24) & 0xff);
	p[3] = (uint8_t) ((data >> 16) & 0xff);
	p[4] = (uint8_t) ((data >> 8) & 0xff);
	p[5] = (uint8_t) (data & 0xff);
}


/**
 * Structure to do a Schwartzian-like transformation, for instance when
 * sorting. If you need a transformation on the objects that are sorted,
 * you can sue this to store the transformed values, so you do not
 * need to do the transformation again for each comparison
 */
struct ldns_schwartzian_compare_struct {
	void *original_object;
	void *transformed_object;
};

/** A general purpose lookup table
 *  
 *  Lookup tables are arrays of (id, name) pairs,
 *  So you can for instance lookup the RCODE 3, which is "NXDOMAIN",
 *  and vice versa. The lookup tables themselves are defined wherever needed,
 *  for instance in \ref host2str.c
 */
struct ldns_struct_lookup_table {
        int id;
        const char *name;
};
typedef struct ldns_struct_lookup_table ldns_lookup_table;
  
/**
 * Looks up the table entry by name, returns NULL if not found.
 * \param[in] table the lookup table to search in
 * \param[in] name what to search for
 * \return the item found
 */
ldns_lookup_table *ldns_lookup_by_name(ldns_lookup_table table[],
                                       const char *name);

/**
 * Looks up the table entry by id, returns NULL if not found.
 * \param[in] table the lookup table to search in
 * \param[in] id what to search for
 * \return the item found
 */
ldns_lookup_table *ldns_lookup_by_id(ldns_lookup_table table[], int id);

/**
 * Returns the value of the specified bit
 * The bits are counted from left to right, so bit #0 is the
 * left most bit.
 * \param[in] bits array holding the bits
 * \param[in] index to the wanted bit
 * \return 
 */
int ldns_get_bit(uint8_t bits[], size_t index);


/**
 * Returns the value of the specified bit
 * The bits are counted from right to left, so bit #0 is the
 * right most bit.
 * \param[in] bits array holding the bits
 * \param[in] index to the wanted bit
 * \return 1 or 0 depending no the bit state
 */
int ldns_get_bit_r(uint8_t bits[], size_t index);

/**
 * sets the specified bit in the specified byte to
 * 1 if value is true, 0 if false
 * The bits are counted from right to left, so bit #0 is the
 * right most bit.
 * \param[in] byte the bit to set the bit in
 * \param[in] bit_nr the bit to set (0 <= n <= 7)
 * \param[in] value whether to set the bit to 1 or 0
 * \return 1 or 0 depending no the bit state
 */
void ldns_set_bit(uint8_t *byte, int bit_nr, bool value);

/**
 * Returns the value of a to the power of b
 * (or 1 of b < 1)
 */
/*@unused@*/
INLINE long
ldns_power(long a, long b) {
	long result = 1;
	while (b > 0) {
		if (b & 1) {
			result *= a;
			if (b == 1) {
				return result;
			}
		}
		a *= a;
		b /= 2;
	}
	return result;
}

/**
 * Returns the int value of the given (hex) digit
 * \param[in] ch the hex char to convert
 * \return the converted decimal value
 */
int ldns_hexdigit_to_int(char ch);

/**
 * Returns the char (hex) representation of the given int
 * \param[in] ch the int to convert
 * \return the converted hex char
 */
char ldns_int_to_hexdigit(int ch);

/**
 * Converts a hex string to binary data
 *
 * \param[out] data The binary result is placed here.
 * At least strlen(str)/2 bytes should be allocated
 * \param[in] str The hex string to convert.
 * This string should not contain spaces
 * \return The number of bytes of converted data, or -1 if one of the arguments * is NULL, or -2 if the string length is not an even number
 */
int
ldns_hexstring_to_data(uint8_t *data, const char *str);

/**
 * Show the internal library version
 * \return a string with the version in it
 */
const char * ldns_version(void);

/**
 * Convert TM to seconds since epoch (midnight, January 1st, 1970).
 * Like timegm(3), which is not always available.
 * \param[in] tm a struct tm* with the date
 * \return the seconds since epoch
 */
time_t ldns_mktime_from_utc(const struct tm *tm);

time_t mktime_from_utc(const struct tm *tm);

/**
 * The function interprets time as the number of seconds since epoch
 * with respect to now using serial arithmitics (rfc1982).
 * That number of seconds is then converted to broken-out time information.
 * This is especially useful when converting the inception and expiration
 * fields of RRSIG records.
 *
 * \param[in] time number of seconds since epoch (midnight, January 1st, 1970)
 *            to be intepreted as a serial arithmitics number relative to now.
 * \param[in] now number of seconds since epoch (midnight, January 1st, 1970)
 *            to which the time value is compared to determine the final value.
 * \param[out] result the struct with the broken-out time information
 * \return result on success or NULL on error
 */
struct tm * ldns_serial_arithmitics_gmtime_r(int32_t time, time_t now, struct tm *result);
 
/**
 * Seed the random function.
 * If the file descriptor is specified, the random generator is seeded with
 * data from that file. If not, /dev/urandom is used.
 *
 * applications should call this if they need entropy data within ldns
 * If openSSL is available, it is automatically seeded from /dev/urandom
 * or /dev/random.
 *
 * If you need more entropy, or have no openssl available, this function
 * MUST be called at the start of the program
 *
 * If openssl *is* available, this function just adds more entropy
 *
 * \param[in] fd a file providing entropy data for the seed
 * \param[in] size the number of bytes to use as entropy data. If this is 0,
 *            only the minimal amount is taken (usually 4 bytes)
 * \return 0 if seeding succeeds, 1 if it fails
 */
int ldns_init_random(FILE *fd, unsigned int size);

/**
 * Get random number.
 * \return random number.
 *
 */
uint16_t ldns_get_random(void);

/**
 * Encode data as BubbleBabble
 *
 * \param[in] data a pointer to data to be encoded
 * \param[in] len size the number of bytes of data
 * \return a string of BubbleBabble
 */
char *ldns_bubblebabble(uint8_t *data, size_t len);


INLINE time_t ldns_time(time_t *t) { return time(t); }


/**
 * calculates the size needed to store the result of b32_ntop
 */
/*@unused@*/
INLINE size_t ldns_b32_ntop_calculate_size(size_t src_data_length)
{
	return src_data_length == 0 ? 0 : ((src_data_length - 1) / 5 + 1) * 8;
}

INLINE size_t ldns_b32_ntop_calculate_size_no_padding(size_t src_data_length)
{
	return ((src_data_length + 3) * 8 / 5) - 4;
}

int ldns_b32_ntop(const uint8_t* src_data, size_t src_data_length,
	     char* target_text_buffer, size_t target_text_buffer_size);

int ldns_b32_ntop_extended_hex(const uint8_t* src_data, size_t src_data_length,
	     char* target_text_buffer, size_t target_text_buffer_size);

#if ! LDNS_BUILD_CONFIG_HAVE_B32_NTOP

int b32_ntop(const uint8_t* src_data, size_t src_data_length,
	     char* target_text_buffer, size_t target_text_buffer_size);

int b32_ntop_extended_hex(const uint8_t* src_data, size_t src_data_length,
	     char* target_text_buffer, size_t target_text_buffer_size);

#endif /* ! LDNS_BUILD_CONFIG_HAVE_B32_NTOP */


/**
 * calculates the size needed to store the result of b32_pton
 */
/*@unused@*/
INLINE size_t ldns_b32_pton_calculate_size(size_t src_text_length)
{
	return src_text_length * 5 / 8;
}

int ldns_b32_pton(const char* src_text, size_t src_text_length,
	       	uint8_t* target_data_buffer, size_t target_data_buffer_size);

int ldns_b32_pton_extended_hex(const char* src_text, size_t src_text_length,
		uint8_t* target_data_buffer, size_t target_data_buffer_size);

#if ! LDNS_BUILD_CONFIG_HAVE_B32_PTON

int b32_pton(const char* src_text, size_t src_text_length,
	       	uint8_t* target_data_buffer, size_t target_data_buffer_size);

int b32_pton_extended_hex(const char* src_text, size_t src_text_length,
		uint8_t* target_data_buffer, size_t target_data_buffer_size);

#endif /* ! LDNS_BUILD_CONFIG_HAVE_B32_PTON */


#ifdef __cplusplus
}
#endif

#endif /* !_UTIL_H */
