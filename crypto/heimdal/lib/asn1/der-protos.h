/* This is a generated file */
#ifndef __der_protos_h__
#define __der_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int
copy_heim_any (
	const heim_any */*from*/,
	heim_any */*to*/);

int
copy_heim_any_set (
	const heim_any_set */*from*/,
	heim_any_set */*to*/);

int
decode_heim_any (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_any */*data*/,
	size_t */*size*/);

int
decode_heim_any_set (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_any_set */*data*/,
	size_t */*size*/);

int
der_copy_bit_string (
	const heim_bit_string */*from*/,
	heim_bit_string */*to*/);

int
der_copy_bmp_string (
	const heim_bmp_string */*from*/,
	heim_bmp_string */*to*/);

int
der_copy_general_string (
	const heim_general_string */*from*/,
	heim_general_string */*to*/);

int
der_copy_generalized_time (
	const time_t */*from*/,
	time_t */*to*/);

int
der_copy_heim_integer (
	const heim_integer */*from*/,
	heim_integer */*to*/);

int
der_copy_ia5_string (
	const heim_ia5_string */*from*/,
	heim_ia5_string */*to*/);

int
der_copy_integer (
	const int */*from*/,
	int */*to*/);

int
der_copy_octet_string (
	const heim_octet_string */*from*/,
	heim_octet_string */*to*/);

int
der_copy_oid (
	const heim_oid */*from*/,
	heim_oid */*to*/);

int
der_copy_printable_string (
	const heim_printable_string */*from*/,
	heim_printable_string */*to*/);

int
der_copy_universal_string (
	const heim_universal_string */*from*/,
	heim_universal_string */*to*/);

int
der_copy_unsigned (
	const unsigned */*from*/,
	unsigned */*to*/);

int
der_copy_utctime (
	const time_t */*from*/,
	time_t */*to*/);

int
der_copy_utf8string (
	const heim_utf8_string */*from*/,
	heim_utf8_string */*to*/);

int
der_copy_visible_string (
	const heim_visible_string */*from*/,
	heim_visible_string */*to*/);

void
der_free_bit_string (heim_bit_string */*k*/);

void
der_free_bmp_string (heim_bmp_string */*k*/);

void
der_free_general_string (heim_general_string */*str*/);

void
der_free_generalized_time (time_t */*t*/);

void
der_free_heim_integer (heim_integer */*k*/);

void
der_free_ia5_string (heim_ia5_string */*str*/);

void
der_free_integer (int */*i*/);

void
der_free_octet_string (heim_octet_string */*k*/);

void
der_free_oid (heim_oid */*k*/);

void
der_free_printable_string (heim_printable_string */*str*/);

void
der_free_universal_string (heim_universal_string */*k*/);

void
der_free_unsigned (unsigned */*u*/);

void
der_free_utctime (time_t */*t*/);

void
der_free_utf8string (heim_utf8_string */*str*/);

void
der_free_visible_string (heim_visible_string */*str*/);

int
der_get_bit_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_bit_string */*data*/,
	size_t */*size*/);

int
der_get_bmp_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_bmp_string */*data*/,
	size_t */*size*/);

int
der_get_boolean (
	const unsigned char */*p*/,
	size_t /*len*/,
	int */*data*/,
	size_t */*size*/);

const char *
der_get_class_name (unsigned /*num*/);

int
der_get_class_num (const char */*name*/);

int
der_get_general_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_general_string */*str*/,
	size_t */*size*/);

int
der_get_generalized_time (
	const unsigned char */*p*/,
	size_t /*len*/,
	time_t */*data*/,
	size_t */*size*/);

int
der_get_heim_integer (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_integer */*data*/,
	size_t */*size*/);

int
der_get_ia5_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_ia5_string */*str*/,
	size_t */*size*/);

int
der_get_integer (
	const unsigned char */*p*/,
	size_t /*len*/,
	int */*ret*/,
	size_t */*size*/);

int
der_get_length (
	const unsigned char */*p*/,
	size_t /*len*/,
	size_t */*val*/,
	size_t */*size*/);

int
der_get_octet_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_octet_string */*data*/,
	size_t */*size*/);

int
der_get_octet_string_ber (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_octet_string */*data*/,
	size_t */*size*/);

int
der_get_oid (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_oid */*data*/,
	size_t */*size*/);

int
der_get_printable_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_printable_string */*str*/,
	size_t */*size*/);

int
der_get_tag (
	const unsigned char */*p*/,
	size_t /*len*/,
	Der_class */*class*/,
	Der_type */*type*/,
	unsigned int */*tag*/,
	size_t */*size*/);

const char *
der_get_tag_name (unsigned /*num*/);

int
der_get_tag_num (const char */*name*/);

const char *
der_get_type_name (unsigned /*num*/);

int
der_get_type_num (const char */*name*/);

int
der_get_universal_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_universal_string */*data*/,
	size_t */*size*/);

int
der_get_unsigned (
	const unsigned char */*p*/,
	size_t /*len*/,
	unsigned */*ret*/,
	size_t */*size*/);

int
der_get_utctime (
	const unsigned char */*p*/,
	size_t /*len*/,
	time_t */*data*/,
	size_t */*size*/);

int
der_get_utf8string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_utf8_string */*str*/,
	size_t */*size*/);

int
der_get_visible_string (
	const unsigned char */*p*/,
	size_t /*len*/,
	heim_visible_string */*str*/,
	size_t */*size*/);

int
der_heim_bit_string_cmp (
	const heim_bit_string */*p*/,
	const heim_bit_string */*q*/);

int
der_heim_bmp_string_cmp (
	const heim_bmp_string */*p*/,
	const heim_bmp_string */*q*/);

int
der_heim_integer_cmp (
	const heim_integer */*p*/,
	const heim_integer */*q*/);

int
der_heim_octet_string_cmp (
	const heim_octet_string */*p*/,
	const heim_octet_string */*q*/);

int
der_heim_oid_cmp (
	const heim_oid */*p*/,
	const heim_oid */*q*/);

int
der_heim_universal_string_cmp (
	const heim_universal_string */*p*/,
	const heim_universal_string */*q*/);

int
der_ia5_string_cmp (
	const heim_ia5_string */*p*/,
	const heim_ia5_string */*q*/);

size_t
der_length_bit_string (const heim_bit_string */*k*/);

size_t
der_length_bmp_string (const heim_bmp_string */*data*/);

size_t
der_length_boolean (const int */*k*/);

size_t
der_length_enumerated (const unsigned */*data*/);

size_t
der_length_general_string (const heim_general_string */*data*/);

size_t
der_length_generalized_time (const time_t */*t*/);

size_t
der_length_heim_integer (const heim_integer */*k*/);

size_t
der_length_ia5_string (const heim_ia5_string */*data*/);

size_t
der_length_integer (const int */*data*/);

size_t
der_length_len (size_t /*len*/);

size_t
der_length_octet_string (const heim_octet_string */*k*/);

size_t
der_length_oid (const heim_oid */*k*/);

size_t
der_length_printable_string (const heim_printable_string */*data*/);

size_t
der_length_tag (unsigned int /*tag*/);

size_t
der_length_universal_string (const heim_universal_string */*data*/);

size_t
der_length_unsigned (const unsigned */*data*/);

size_t
der_length_utctime (const time_t */*t*/);

size_t
der_length_utf8string (const heim_utf8_string */*data*/);

size_t
der_length_visible_string (const heim_visible_string */*data*/);

int
der_match_tag (
	const unsigned char */*p*/,
	size_t /*len*/,
	Der_class /*class*/,
	Der_type /*type*/,
	unsigned int /*tag*/,
	size_t */*size*/);

int
der_match_tag2 (
	const unsigned char */*p*/,
	size_t /*len*/,
	Der_class /*class*/,
	Der_type */*type*/,
	unsigned int /*tag*/,
	size_t */*size*/);

int
der_match_tag_and_length (
	const unsigned char */*p*/,
	size_t /*len*/,
	Der_class /*class*/,
	Der_type */*type*/,
	unsigned int /*tag*/,
	size_t */*length_ret*/,
	size_t */*size*/);

int
der_parse_heim_oid (
	const char */*str*/,
	const char */*sep*/,
	heim_oid */*data*/);

int
der_parse_hex_heim_integer (
	const char */*p*/,
	heim_integer */*data*/);

int
der_print_heim_oid (
	const heim_oid */*oid*/,
	char /*delim*/,
	char **/*str*/);

int
der_print_hex_heim_integer (
	const heim_integer */*data*/,
	char **/*p*/);

int
der_printable_string_cmp (
	const heim_printable_string */*p*/,
	const heim_printable_string */*q*/);

int
der_put_bit_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_bit_string */*data*/,
	size_t */*size*/);

int
der_put_bmp_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_bmp_string */*data*/,
	size_t */*size*/);

int
der_put_boolean (
	unsigned char */*p*/,
	size_t /*len*/,
	const int */*data*/,
	size_t */*size*/);

int
der_put_general_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_general_string */*str*/,
	size_t */*size*/);

int
der_put_generalized_time (
	unsigned char */*p*/,
	size_t /*len*/,
	const time_t */*data*/,
	size_t */*size*/);

int
der_put_heim_integer (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_integer */*data*/,
	size_t */*size*/);

int
der_put_ia5_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_ia5_string */*str*/,
	size_t */*size*/);

int
der_put_integer (
	unsigned char */*p*/,
	size_t /*len*/,
	const int */*v*/,
	size_t */*size*/);

int
der_put_length (
	unsigned char */*p*/,
	size_t /*len*/,
	size_t /*val*/,
	size_t */*size*/);

int
der_put_length_and_tag (
	unsigned char */*p*/,
	size_t /*len*/,
	size_t /*len_val*/,
	Der_class /*class*/,
	Der_type /*type*/,
	unsigned int /*tag*/,
	size_t */*size*/);

int
der_put_octet_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_octet_string */*data*/,
	size_t */*size*/);

int
der_put_oid (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_oid */*data*/,
	size_t */*size*/);

int
der_put_printable_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_printable_string */*str*/,
	size_t */*size*/);

int
der_put_tag (
	unsigned char */*p*/,
	size_t /*len*/,
	Der_class /*class*/,
	Der_type /*type*/,
	unsigned int /*tag*/,
	size_t */*size*/);

int
der_put_universal_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_universal_string */*data*/,
	size_t */*size*/);

int
der_put_unsigned (
	unsigned char */*p*/,
	size_t /*len*/,
	const unsigned */*v*/,
	size_t */*size*/);

int
der_put_utctime (
	unsigned char */*p*/,
	size_t /*len*/,
	const time_t */*data*/,
	size_t */*size*/);

int
der_put_utf8string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_utf8_string */*str*/,
	size_t */*size*/);

int
der_put_visible_string (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_visible_string */*str*/,
	size_t */*size*/);

int
encode_heim_any (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_any */*data*/,
	size_t */*size*/);

int
encode_heim_any_set (
	unsigned char */*p*/,
	size_t /*len*/,
	const heim_any_set */*data*/,
	size_t */*size*/);

void
free_heim_any (heim_any */*data*/);

void
free_heim_any_set (heim_any_set */*data*/);

int
heim_any_cmp (
	const heim_any_set */*p*/,
	const heim_any_set */*q*/);

size_t
length_heim_any (const heim_any */*data*/);

size_t
length_heim_any_set (const heim_any */*data*/);

#ifdef __cplusplus
}
#endif

#endif /* __der_protos_h__ */
