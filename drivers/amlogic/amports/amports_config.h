#ifndef AMPORTS_CONFIG_HHH
#define AMPORTS_CONFIG_HHH
#include <mach/cpu.h>
#include <linux/kconfig.h>

/*
value seem:
arch\arm\plat-meson\include\plat\cpu.h
*/

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8B
#define HAS_VPU_PROT  0
#define HAS_VDEC2     0
#define HAS_HEVC_VDEC 1
#define HAS_HDEC      1

#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define HAS_VPU_PROT  1
#define HAS_VDEC2     (IS_MESON_M8_CPU ? 1 : 0)
#define HAS_HEVC_VDEC (IS_MESON_M8_CPU ? 0 : 1)
#define HAS_HDEC      1

#elif  MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
#define HAS_VPU_PROT  0
#define HAS_VDEC2     1
#define HAS_HDEC      1
#define HAS_HEVC_VDEC 0

#else
#define HAS_VPU_PROT  0
#define HAS_VDEC2     0
#define HAS_HEVC_VDEC 0
#define HAS_HDEC      0

#endif

#ifndef CONFIG_AM_VDEC_H265
#undef HAS_HEVC_VDEC
#define HAS_HEVC_VDEC   0
#endif

#endif //AMPORTS_CONFIG_HHH

