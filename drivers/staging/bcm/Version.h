/*Copyright (c) 2005 Beceem Communications Inc.

Module Name:

  Version.h

Abstract:


--*/

#ifndef VERSION_H
#define VERSION_H


#define VER_FILETYPE                VFT_DRV
#define VER_FILESUBTYPE             VFT2_DRV_NETWORK

#define VER_FILEVERSION             5.2.45
#define VER_FILEVERSION_STR         "5.2.45"

#undef VER_PRODUCTVERSION
#define VER_PRODUCTVERSION          VER_FILEVERSION

#undef VER_PRODUCTVERSION_STR
#define VER_PRODUCTVERSION_STR      VER_FILEVERSION_STR


#endif /* VERSION_H */
