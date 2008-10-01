//
// Only define one of follow
//

#ifdef WB_WIN
	#define VER_FILEVERSION             1,00,47,00
	#define VER_FILEVERSION_STR         "1.00.47.00"
	#define WB32_DRIVER_MAJOR_VERSION   0x0100
	#define WB32_DRIVER_MINOR_VERSION   0x4700
#endif

#ifdef WB_CE
	#define VER_FILEVERSION             2,00,47,00
	#define VER_FILEVERSION_STR         "2.00.47.00"
	#define WB32_DRIVER_MAJOR_VERSION   0x0200
	#define WB32_DRIVER_MINOR_VERSION   0x4700
#endif

#ifdef WB_LINUX
	#define VER_FILEVERSION             3,00,47,00
	#define VER_FILEVERSION_STR         "3.00.47.00"
	#define WB32_DRIVER_MAJOR_VERSION   0x0300
	#define WB32_DRIVER_MINOR_VERSION   0x4700
#endif






