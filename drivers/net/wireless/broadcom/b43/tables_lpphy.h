#ifndef B43_TABLES_LPPHY_H_
#define B43_TABLES_LPPHY_H_


#define B43_LPTAB_TYPEMASK		0xF0000000
#define B43_LPTAB_8BIT			0x10000000
#define B43_LPTAB_16BIT			0x20000000
#define B43_LPTAB_32BIT			0x30000000
#define B43_LPTAB8(table, offset)	(((table) << 10) | (offset) | B43_LPTAB_8BIT)
#define B43_LPTAB16(table, offset)	(((table) << 10) | (offset) | B43_LPTAB_16BIT)
#define B43_LPTAB32(table, offset)	(((table) << 10) | (offset) | B43_LPTAB_32BIT)

/* Table definitions */
#define B43_LPTAB_TXPWR_R2PLUS		B43_LPTAB32(0x07, 0) /* TX power lookup table (rev >= 2) */
#define B43_LPTAB_TXPWR_R0_1		B43_LPTAB32(0xA0, 0) /* TX power lookup table (rev < 2) */

u32 b43_lptab_read(struct b43_wldev *dev, u32 offset);
void b43_lptab_write(struct b43_wldev *dev, u32 offset, u32 value);

/* Bulk table access. Note that these functions return the bulk data in
 * host endianness! The returned data is _not_ a bytearray, but an array
 * consisting of nr_elements of the data type. */
void b43_lptab_read_bulk(struct b43_wldev *dev, u32 offset,
			 unsigned int nr_elements, void *data);
void b43_lptab_write_bulk(struct b43_wldev *dev, u32 offset,
			  unsigned int nr_elements, const void *data);

void b2062_upload_init_table(struct b43_wldev *dev);
void b2063_upload_init_table(struct b43_wldev *dev);

struct lpphy_tx_gain_table_entry {
	u8 gm,  pga,  pad,  dac,  bb_mult;
};

void lpphy_write_gain_table(struct b43_wldev *dev, int offset,
			    struct lpphy_tx_gain_table_entry data);
void lpphy_write_gain_table_bulk(struct b43_wldev *dev, int offset, int count,
				 struct lpphy_tx_gain_table_entry *table);

void lpphy_rev0_1_table_init(struct b43_wldev *dev);
void lpphy_rev2plus_table_init(struct b43_wldev *dev);
void lpphy_init_tx_gain_table(struct b43_wldev *dev);

#endif /* B43_TABLES_LPPHY_H_ */
