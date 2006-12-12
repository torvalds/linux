
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef DSP_DEFS_H_  
#define DSP_DEFS_H_
#include "dspdids.h"
/*---------------------------------------------------------------------------*/
#define dsp_download_reserve_space(fp,length)
/*****************************************************************************/
/*
 * OS file access abstraction layer
 *
 * I/O functions returns -1 on error, 0 on EOF
 */
struct _OsFileHandle_;
typedef long (  * OsFileIo)  (struct _OsFileHandle_    *handle,
                                void                     *buffer,
                                long                       size) ;
typedef long (  * OsFileSeek)(struct _OsFileHandle_    *handle,
                                long                       position,
                                int                        mode) ;
typedef long (  * OsCardLoad)(struct _OsFileHandle_    *handle,
                                long                       length,
                                void                         *   *addr) ;
typedef struct _OsFileHandle_
{ void       *sysFileDesc ;
 unsigned long sysFileSize ;
 OsFileIo      sysFileRead ;
 OsFileSeek    sysFileSeek ;
 void       *sysLoadDesc ;
 OsCardLoad    sysCardLoad ;
} OsFileHandle ;
extern OsFileHandle *OsOpenFile (char *path_name) ;
extern void          OsCloseFile (OsFileHandle *fp) ;
/*****************************************************************************/
#define DSP_TELINDUS_FILE "dspdload.bin"
/* special DSP file for BRI cards for Qsig and CornetN because of missing memory */
#define DSP_QSIG_TELINDUS_FILE "dspdqsig.bin"
#define DSP_MDM_TELINDUS_FILE "dspdvmdm.bin"
#define DSP_FAX_TELINDUS_FILE "dspdvfax.bin"
#define DSP_DIRECTORY_ENTRIES 64
#define DSP_MEMORY_TYPE_EXTERNAL_DM         0
#define DSP_MEMORY_TYPE_EXTERNAL_PM         1
#define DSP_MEMORY_TYPE_INTERNAL_DM         2
#define DSP_MEMORY_TYPE_INTERNAL_PM         3
#define DSP_DOWNLOAD_FLAG_BOOTABLE          0x0001
#define DSP_DOWNLOAD_FLAG_2181              0x0002
#define DSP_DOWNLOAD_FLAG_TIMECRITICAL      0x0004
#define DSP_DOWNLOAD_FLAG_COMPAND           0x0008
#define DSP_MEMORY_BLOCK_COUNT              16
#define DSP_SEGMENT_PM_FLAG                 0x0001
#define DSP_SEGMENT_SHARED_FLAG             0x0002
#define DSP_SEGMENT_EXTERNAL_DM             DSP_MEMORY_TYPE_EXTERNAL_DM
#define DSP_SEGMENT_EXTERNAL_PM             DSP_MEMORY_TYPE_EXTERNAL_PM
#define DSP_SEGMENT_INTERNAL_DM             DSP_MEMORY_TYPE_INTERNAL_DM
#define DSP_SEGMENT_INTERNAL_PM             DSP_MEMORY_TYPE_INTERNAL_PM
#define DSP_SEGMENT_FIRST_RELOCATABLE       4
#define DSP_DATA_BLOCK_PM_FLAG              0x0001
#define DSP_DATA_BLOCK_DWORD_FLAG           0x0002
#define DSP_DATA_BLOCK_RESOLVE_FLAG         0x0004
#define DSP_RELOC_NONE                      0x00
#define DSP_RELOC_SEGMENT_MASK              0x3f
#define DSP_RELOC_TYPE_MASK                 0xc0
#define DSP_RELOC_TYPE_0                    0x00  /* relocation of address in DM word / high part of PM word */
#define DSP_RELOC_TYPE_1                    0x40  /* relocation of address in low part of PM data word */
#define DSP_RELOC_TYPE_2                    0x80  /* relocation of address in standard command */
#define DSP_RELOC_TYPE_3                    0xc0  /* relocation of address in call/jump on flag in */
#define DSP_COMBIFILE_FORMAT_IDENTIFICATION_SIZE 48
#define DSP_COMBIFILE_FORMAT_VERSION_BCD    0x0100
#define DSP_FILE_FORMAT_IDENTIFICATION_SIZE 48
#define DSP_FILE_FORMAT_VERSION_BCD         0x0100
typedef struct tag_dsp_combifile_header
{
  char                  format_identification[DSP_COMBIFILE_FORMAT_IDENTIFICATION_SIZE];
  word                  format_version_bcd;
  word                  header_size;
  word                  combifile_description_size;
  word                  directory_entries;
  word                  directory_size;
  word                  download_count;
  word                  usage_mask_size;
} t_dsp_combifile_header;
typedef struct tag_dsp_combifile_directory_entry
{
  word                  card_type_number;
  word                  file_set_number;
} t_dsp_combifile_directory_entry;
typedef struct tag_dsp_file_header
{
  char                  format_identification[DSP_FILE_FORMAT_IDENTIFICATION_SIZE];
  word                  format_version_bcd;
  word                  download_id;
  word                  download_flags;
  word                  required_processing_power;
  word                  interface_channel_count;
  word                  header_size;
  word                  download_description_size;
  word                  memory_block_table_size;
  word                  memory_block_count;
  word                  segment_table_size;
  word                  segment_count;
  word                  symbol_table_size;
  word                  symbol_count;
  word                  total_data_size_dm;
  word                  data_block_count_dm;
  word                  total_data_size_pm;
  word                  data_block_count_pm;
} t_dsp_file_header;
typedef struct tag_dsp_memory_block_desc
{
  word                  alias_memory_block;
  word                  memory_type;
  word                  address;
  word                  size;             /* DSP words */
} t_dsp_memory_block_desc;
typedef struct tag_dsp_segment_desc
{
  word                  memory_block;
  word                  attributes;
  word                  base;
  word                  size;
  word                  alignment;        /* ==0 -> no other legal start address than base */
} t_dsp_segment_desc;
typedef struct tag_dsp_symbol_desc
{
  word                  symbol_id;
  word                  segment;
  word                  offset;
  word                  size;             /* DSP words */
} t_dsp_symbol_desc;
typedef struct tag_dsp_data_block_header
{
  word                  attributes;
  word                  segment;
  word                  offset;
  word                  size;             /* DSP words */
} t_dsp_data_block_header;
typedef struct tag_dsp_download_desc
{
  word                  download_id;
  word                  download_flags;
  word                  required_processing_power;
  word                  interface_channel_count;
  word                  excess_header_size;
  word                  memory_block_count;
  word                  segment_count;
  word                  symbol_count;
  word                  data_block_count_dm;
  word                  data_block_count_pm;
  byte   *            p_excess_header_data;
  char   *            p_download_description;
  t_dsp_memory_block_desc   *p_memory_block_table;
  t_dsp_segment_desc   *p_segment_table;
  t_dsp_symbol_desc   *p_symbol_table;
  word   *            p_data_blocks_dm;
  word   *            p_data_blocks_pm;
} t_dsp_desc;
typedef struct tag_dsp_portable_download_desc /* be sure to keep native alignment for MAESTRA's */
{
  word                  download_id;
  word                  download_flags;
  word                  required_processing_power;
  word                  interface_channel_count;
  word                  excess_header_size;
  word                  memory_block_count;
  word                  segment_count;
  word                  symbol_count;
  word                  data_block_count_dm;
  word                  data_block_count_pm;
  dword                 p_excess_header_data;
  dword                 p_download_description;
  dword                 p_memory_block_table;
  dword                 p_segment_table;
  dword                 p_symbol_table;
  dword                 p_data_blocks_dm;
  dword                 p_data_blocks_pm;
} t_dsp_portable_desc;
#define DSP_DOWNLOAD_INDEX_KERNEL               0
#define DSP30TX_DOWNLOAD_INDEX_KERNEL           1
#define DSP30RX_DOWNLOAD_INDEX_KERNEL           2
#define DSP_MAX_DOWNLOAD_COUNT                  64
#define DSP_DOWNLOAD_MAX_SEGMENTS         16
#define DSP_UDATA_REQUEST_RECONFIGURE     0
/*
parameters:
  <word> reconfigure delay (in 8kHz samples)
  <word> reconfigure code
  <byte> reconfigure hdlc preamble flags
*/
#define DSP_RECONFIGURE_TX_FLAG           0x8000
#define DSP_RECONFIGURE_SHORT_TRAIN_FLAG  0x4000
#define DSP_RECONFIGURE_ECHO_PROTECT_FLAG 0x2000
#define DSP_RECONFIGURE_HDLC_FLAG         0x1000
#define DSP_RECONFIGURE_SYNC_FLAG         0x0800
#define DSP_RECONFIGURE_PROTOCOL_MASK     0x00ff
#define DSP_RECONFIGURE_IDLE              0
#define DSP_RECONFIGURE_V25               1
#define DSP_RECONFIGURE_V21_CH2           2
#define DSP_RECONFIGURE_V27_2400          3
#define DSP_RECONFIGURE_V27_4800          4
#define DSP_RECONFIGURE_V29_7200          5
#define DSP_RECONFIGURE_V29_9600          6
#define DSP_RECONFIGURE_V33_12000         7
#define DSP_RECONFIGURE_V33_14400         8
#define DSP_RECONFIGURE_V17_7200          9
#define DSP_RECONFIGURE_V17_9600          10
#define DSP_RECONFIGURE_V17_12000         11
#define DSP_RECONFIGURE_V17_14400         12
/*
data indications if transparent framer
  <byte> data 0
  <byte> data 1
  ...
data indications if HDLC framer
  <byte> data 0
  <byte> data 1
  ...
  <byte> CRC 0
  <byte> CRC 1
  <byte> preamble flags
*/
#define DSP_UDATA_INDICATION_SYNC         0
/*
returns:
  <word> time of sync (sampled from counter at 8kHz)
*/
#define DSP_UDATA_INDICATION_DCD_OFF      1
/*
returns:
  <word> time of DCD off (sampled from counter at 8kHz)
*/
#define DSP_UDATA_INDICATION_DCD_ON       2
/*
returns:
  <word> time of DCD on (sampled from counter at 8kHz)
  <byte> connected norm
  <word> connected options
  <dword> connected speed (bit/s)
*/
#define DSP_UDATA_INDICATION_CTS_OFF      3
/*
returns:
  <word> time of CTS off (sampled from counter at 8kHz)
*/
#define DSP_UDATA_INDICATION_CTS_ON       4
/*
returns:
  <word> time of CTS on (sampled from counter at 8kHz)
  <byte> connected norm
  <word> connected options
  <dword> connected speed (bit/s)
*/
#define DSP_CONNECTED_NORM_UNSPECIFIED      0
#define DSP_CONNECTED_NORM_V21              1
#define DSP_CONNECTED_NORM_V23              2
#define DSP_CONNECTED_NORM_V22              3
#define DSP_CONNECTED_NORM_V22_BIS          4
#define DSP_CONNECTED_NORM_V32_BIS          5
#define DSP_CONNECTED_NORM_V34              6
#define DSP_CONNECTED_NORM_V8               7
#define DSP_CONNECTED_NORM_BELL_212A        8
#define DSP_CONNECTED_NORM_BELL_103         9
#define DSP_CONNECTED_NORM_V29_LEASED_LINE  10
#define DSP_CONNECTED_NORM_V33_LEASED_LINE  11
#define DSP_CONNECTED_NORM_TFAST            12
#define DSP_CONNECTED_NORM_V21_CH2          13
#define DSP_CONNECTED_NORM_V27_TER          14
#define DSP_CONNECTED_NORM_V29              15
#define DSP_CONNECTED_NORM_V33              16
#define DSP_CONNECTED_NORM_V17              17
#define DSP_CONNECTED_OPTION_TRELLIS        0x0001
/*---------------------------------------------------------------------------*/
extern char *dsp_read_file (OsFileHandle          *fp,
                            word                     card_type_number,
                            word                  *p_dsp_download_count,
                            t_dsp_desc            *p_dsp_download_table,
                            t_dsp_portable_desc   *p_dsp_portable_download_table) ;
/*---------------------------------------------------------------------------*/
#endif /* DSP_DEFS_H_ */  
