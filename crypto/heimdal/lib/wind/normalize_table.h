/* ./normalize_table.h */
/* Automatically generated at 2012-01-11T14:07:01.713967 */

#ifndef NORMALIZE_TABLE_H
#define NORMALIZE_TABLE_H 1

#include <krb5-types.h>

#define MAX_LENGTH_CANON 18

struct translation {
  uint32_t key;
  unsigned short val_len;
  unsigned short val_offset;
};

extern const struct translation _wind_normalize_table[];

extern const uint32_t _wind_normalize_val_table[];

extern const size_t _wind_normalize_table_size;

struct canon_node {
  uint32_t val;
  unsigned char next_start;
  unsigned char next_end;
  unsigned short next_offset;
};

extern const struct canon_node _wind_canon_table[];

extern const unsigned short _wind_canon_next_table[];
#endif /* NORMALIZE_TABLE_H */
