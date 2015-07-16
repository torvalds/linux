#ifndef __NVKM_DEBUG_H__
#define __NVKM_DEBUG_H__
extern int nv_info_debug_level;

#define NV_DBG_FATAL    0
#define NV_DBG_ERROR    1
#define NV_DBG_WARN     2
#define NV_DBG_INFO     nv_info_debug_level
#define NV_DBG_DEBUG    4
#define NV_DBG_TRACE    5
#define NV_DBG_PARANOIA 6
#define NV_DBG_SPAM     7

#define NV_DBG_INFO_NORMAL 3
#define NV_DBG_INFO_SILENT NV_DBG_DEBUG

#define nv_debug_level(a) nv_info_debug_level = NV_DBG_INFO_##a
#endif
