//------------------------------------------------------------------------------
// Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef __REG_DBSCHEMA_H__
#define __REG_DBSCHEMA_H__

/*
 * This file describes the regulatory DB schema, which is common between the
 * 'generator' and 'parser'. The 'generator' runs on a host(typically a x86
 * Linux) and spits outs two binary files, which follow the DB file
 * format(described below). The resultant output "regulatoryData_AG.bin"
 * is binary file which has information regarding A and G regulatory
 * information, while the "regulatoryData_G.bin" consists of G-ONLY regulatory
 * information. This binary file is parsed in the target for extracting
 * regulatory information.
 * 
 * The DB values used to populate the regulatory DB are defined in
 * reg_dbvalues.h
 *
 */

/* Binary data file - Representation of Regulatory DB*/
#define REG_DATA_FILE_AG    "./regulatoryData_AG.bin"
#define REG_DATA_FILE_G     "./regulatoryData_G.bin"


/* Table tags used to encode different tables in the database */
enum data_tags_t{
    REG_DMN_PAIR_MAPPING_TAG = 0,
    REG_COUNTRY_CODE_TO_ENUM_RD_TAG,
    REG_DMN_FREQ_BAND_regDmn5GhzFreq_TAG,
    REG_DMN_FREQ_BAND_regDmn2Ghz11_BG_Freq_TAG,
    REG_DOMAIN_TAG,
    MAX_DB_TABLE_TAGS
    };



/*
 ****************************************************************************
 * Regulatory DB file format :
 * 4-bytes : "RGDB" (Magic Key)
 * 4-bytes : version (Default is 5379(my extn))
 * 4-bytes : length of file
 * dbType(4)
 * TAG(4)
 * Entries(1)entrySize(1)searchType(1)reserved[3]tableSize(2)"0xdeadbeef"(4)struct_data....
 * TAG(4)
 * Entries(1)entrySize(1)searchType(1)reserved[3]tableSize(2)"0xdeadbeef"(4)struct_data....
 * TAG(4)
 * Entries(1)entrySize(1)searchType(1)reserved[3]tableSize(2)"0xdeadbeef"(4)struct_data....
 * ...
 * ...
 ****************************************************************************
 *
 */

/*
 * Length of the file would be filled in when the file is created and
 * it would include the header size.
 */

#define REG_DB_KEY          "RGDB" /* Should be EXACTLY 4-bytes */
#define REG_DB_VER           7802  /* Between 0-9999 */
/*  REG_DB_VER history in reverse chronological order: 
 *  7802: 78 (ASCII code of N) + 02 (minor version number) - updated 10/21/09 
 *  7801: 78 (ASCII code of N) + 01 (minor version number, increment on further changes)
 *  1178: '11N' = 11 + ASCII code of N(78)
 *  5379: initial version, no 11N support
 */
#define MAGIC_KEY_OFFSET    0
#define VERSION_OFFSET      4
#define FILE_SZ_OFFSET      8
#define DB_TYPE_OFFSET      12

#define MAGIC_KEY_SZ        4
#define VERSION_SZ          4
#define FILE_SZ_SZ          4
#define DB_TYPE_SZ          4
#define DB_TAG_SZ           4

#define REGDB_GET_MAGICKEY(x)     ((char *)x + MAGIC_KEY_OFFSET) 
#define REGDB_GET_VERSION(x)      ((char *)x + VERSION_OFFSET)
#define REGDB_GET_FILESIZE(x)     *((unsigned int *)((char *)x + FILE_SZ_OFFSET))
#define REGDB_GET_DBTYPE(x)       *((char *)x + DB_TYPE_OFFSET)

#define REGDB_SET_FILESIZE(x, sz_) *((unsigned int *)((char *)x + FILE_SZ_OFFSET)) = (sz_)
#define REGDB_IS_EOF(cur, begin)  ( REGDB_GET_FILESIZE(begin) > ((cur) - (begin)) )


/* A Table can be search based on key as a parameter or accessed directly
 * by giving its index in to the table.
 */
enum searchType {
    KEY_BASED_TABLE_SEARCH = 1,
    INDEX_BASED_TABLE_ACCESS
    };


/* Data is organised as different tables. There is a Master table, which
 * holds information regarding all the tables. It does not have any
 * knowledge about the attributes of the table it is holding
 * but has external view of the same(for ex, how many entries, record size,
 * how to search the table, total table size and reference to the data
 * instance of table).
 */
typedef PREPACK struct dbMasterTable_t {    /* Hold ptrs to Table data structures */
    u8     numOfEntries;
    char entrySize;      /* Entry size per table row */
    char searchType;     /* Index based access or key based */
    char reserved[3];    /* for alignment */
    u16 tableSize;      /* Size of this table */
    char *dataPtr;       /* Ptr to the actual Table */
} POSTPACK dbMasterTable;    /* Master table - table of tables */


/* used to get the number of rows in a table */
#define REGDB_NUM_OF_ROWS(a)    (sizeof (a) / sizeof (a[0]))

/* 
 * Used to set the RegDomain bitmask which chooses which frequency
 * band specs are used.
 */

#define BMLEN 2         /* Use 2 32-bit uint for channel bitmask */
#define BMZERO {0,0}    /* BMLEN zeros */

#define BM(_fa, _fb, _fc, _fd, _fe, _ff, _fg, _fh) \
      {((((_fa >= 0) && (_fa < 32)) ? (((u32) 1) << _fa) : 0) | \
    (((_fb >= 0) && (_fb < 32)) ? (((u32) 1) << _fb) : 0) | \
    (((_fc >= 0) && (_fc < 32)) ? (((u32) 1) << _fc) : 0) | \
    (((_fd >= 0) && (_fd < 32)) ? (((u32) 1) << _fd) : 0) | \
    (((_fe >= 0) && (_fe < 32)) ? (((u32) 1) << _fe) : 0) | \
    (((_ff >= 0) && (_ff < 32)) ? (((u32) 1) << _ff) : 0) | \
    (((_fg >= 0) && (_fg < 32)) ? (((u32) 1) << _fg) : 0) | \
    (((_fh >= 0) && (_fh < 32)) ? (((u32) 1) << _fh) : 0)), \
       ((((_fa > 31) && (_fa < 64)) ? (((u32) 1) << (_fa - 32)) : 0) | \
        (((_fb > 31) && (_fb < 64)) ? (((u32) 1) << (_fb - 32)) : 0) | \
        (((_fc > 31) && (_fc < 64)) ? (((u32) 1) << (_fc - 32)) : 0) | \
        (((_fd > 31) && (_fd < 64)) ? (((u32) 1) << (_fd - 32)) : 0) | \
        (((_fe > 31) && (_fe < 64)) ? (((u32) 1) << (_fe - 32)) : 0) | \
        (((_ff > 31) && (_ff < 64)) ? (((u32) 1) << (_ff - 32)) : 0) | \
        (((_fg > 31) && (_fg < 64)) ? (((u32) 1) << (_fg - 32)) : 0) | \
        (((_fh > 31) && (_fh < 64)) ? (((u32) 1) << (_fh - 32)) : 0))}


/*
 * THE following table is the mapping of regdomain pairs specified by
 * a regdomain value to the individual unitary reg domains
 */

typedef PREPACK struct reg_dmn_pair_mapping {
    u16 regDmnEnum;    /* 16 bit reg domain pair */
    u16 regDmn5GHz;    /* 5GHz reg domain */
    u16 regDmn2GHz;    /* 2GHz reg domain */
    u8 flags5GHz;     /* Requirements flags (AdHoc disallow etc) */
    u8 flags2GHz;     /* Requirements flags (AdHoc disallow etc) */
    u32 pscanMask;     /* Passive Scan flags which can override unitary domain passive scan
                                   flags.  This value is used as a mask on the unitary flags*/
} POSTPACK REG_DMN_PAIR_MAPPING;

#define OFDM_YES (1 << 0)
#define OFDM_NO  (0 << 0)
#define MCS_HT20_YES   (1 << 1)
#define MCS_HT20_NO    (0 << 1)
#define MCS_HT40_A_YES (1 << 2)
#define MCS_HT40_A_NO  (0 << 2)
#define MCS_HT40_G_YES (1 << 3)
#define MCS_HT40_G_NO  (0 << 3)

typedef PREPACK struct {
    u16 countryCode;
    u16 regDmnEnum;
    char isoName[3];
    char allowMode;  /* what mode is allowed - bit 0: OFDM; bit 1: MCS_HT20; bit 2: MCS_HT40_A; bit 3: MCS_HT40_G */
} POSTPACK COUNTRY_CODE_TO_ENUM_RD;

/* lower 16 bits of ht40ChanMask */
#define NO_FREQ_HT40    0x0     /* no freq is HT40 capable */
#define F1_TO_F4_HT40   0xF     /* freq 1 to 4 in the block is ht40 capable */
#define F2_TO_F3_HT40   0x6     /* freq 2 to 3 in the block is ht40 capable */
#define F1_TO_F10_HT40  0x3FF   /* freq 1 to 10 in the block is ht40 capable */
#define F3_TO_F11_HT40  0x7FC   /* freq 3 to 11 in the block is ht40 capable */
#define F3_TO_F9_HT40   0x1FC   /* freq 3 to 9 in the block is ht40 capable */
#define F1_TO_F8_HT40   0xFF    /* freq 1 to 8 in the block is ht40 capable */
#define F1_TO_F4_F9_TO_F10_HT40   0x30F    /* freq 1 to 4, 9 to 10 in the block is ht40 capable */

/* upper 16 bits of ht40ChanMask */
#define FREQ_HALF_RATE      0x10000
#define FREQ_QUARTER_RATE   0x20000

typedef PREPACK struct RegDmnFreqBand {
    u16 lowChannel;     /* Low channel center in MHz */
    u16 highChannel;    /* High Channel center in MHz */
    u8 power;          /* Max power (dBm) for channel range */
    u8 channelSep;     /* Channel separation within the band */
    u8 useDfs;         /* Use DFS in the RegDomain if corresponding bit is set */
    u8 mode;           /* Mode of operation */
    u32 usePassScan;    /* Use Passive Scan in the RegDomain if corresponding bit is set */
    u32 ht40ChanMask;   /* lower 16 bits: indicate which frequencies in the block is HT40 capable
                                   upper 16 bits: what rate (half/quarter) the channel is  */
} POSTPACK REG_DMN_FREQ_BAND;



typedef PREPACK struct regDomain {
    u16 regDmnEnum;     /* value from EnumRd table */
    u8 rdCTL;
    u8 maxAntGain;
    u8 dfsMask;        /* DFS bitmask for 5Ghz tables */
    u8 flags;          /* Requirement flags (AdHoc disallow etc) */
    u16 reserved;       /* for alignment */
    u32 pscan;          /* Bitmask for passive scan */
    u32 chan11a[BMLEN]; /* 64 bit bitmask for channel/band selection */
    u32 chan11bg[BMLEN];/* 64 bit bitmask for channel/band selection */
} POSTPACK REG_DOMAIN;

#endif /* __REG_DBSCHEMA_H__ */
