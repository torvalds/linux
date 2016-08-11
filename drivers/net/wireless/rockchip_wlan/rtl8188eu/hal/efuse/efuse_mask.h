
#if DEV_BUS_TYPE == RT_USB_INTERFACE

	#if defined(CONFIG_RTL8188E)
	#include "rtl8188e/HalEfuseMask8188E_USB.h"
	#endif

	#if defined(CONFIG_RTL8812A)
	#include "rtl8812a/HalEfuseMask8812A_USB.h"
	#endif

	#if defined(CONFIG_RTL8821A)
	#include "rtl8812a/HalEfuseMask8821A_USB.h"
	#endif

	#if defined(CONFIG_RTL8192E)
	#include "rtl8192e/HalEfuseMask8192E_USB.h"
	#endif

	#if defined(CONFIG_RTL8723B)
	#include "rtl8723b/HalEfuseMask8723B_USB.h"
	#endif

	#if defined(CONFIG_RTL8814A)
	#include "rtl8814a/HalEfuseMask8814A_USB.h"
	#endif

	#if defined(CONFIG_RTL8703B)
	#include "rtl8703b/HalEfuseMask8703B_USB.h"
	#endif

	#if defined(CONFIG_RTL8188F)
	#include "rtl8188f/HalEfuseMask8188F_USB.h"
	#endif

#elif DEV_BUS_TYPE == RT_PCI_INTERFACE

	#if defined(CONFIG_RTL8188E)
	#include "rtl8188e/HalEfuseMask8188E_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8812A)
	#include "rtl8812a/HalEfuseMask8812A_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8821A)
	#include "rtl8812a/HalEfuseMask8821A_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8192E)
	#include "rtl8192e/HalEfuseMask8192E_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8723B)
	#include "rtl8723b/HalEfuseMask8723B_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8814A)
	#include "rtl8814a/HalEfuseMask8814A_PCIE.h"
	#endif

	#if defined(CONFIG_RTL8703B)
	#include "rtl8703b/HalEfuseMask8703B_PCIE.h"
	#endif

#elif DEV_BUS_TYPE == RT_SDIO_INTERFACE

	#if defined(CONFIG_RTL8188E)
	#include "rtl8188e/HalEfuseMask8188E_SDIO.h"
	#endif

	#if defined(CONFIG_RTL8703B)
	#include "rtl8703b/HalEfuseMask8703B_SDIO.h"
	#endif

	#if defined(CONFIG_RTL8188F)
	#include "rtl8188f/HalEfuseMask8188F_SDIO.h"
	#endif

#endif