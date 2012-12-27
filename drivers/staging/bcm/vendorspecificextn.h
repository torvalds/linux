
#ifndef __VENDOR_EXTN_NVM_H__
#define __VENDOR_EXTN_NVM_H__

#define CONTINUE_COMMON_PATH 0xFFFF

INT vendorextnGetSectionInfo(PVOID  pContext,PFLASH2X_VENDORSPECIFIC_INFO pVendorInfo);
INT vendorextnExit(struct bcm_mini_adapter *Adapter);
INT vendorextnInit(struct bcm_mini_adapter *Adapter);
INT vendorextnIoctl(struct bcm_mini_adapter *Adapter, UINT cmd, ULONG arg);
INT vendorextnReadSection(PVOID  pContext, PUCHAR pBuffer, enum bcm_flash2x_section_val SectionVal,
			UINT offset, UINT numOfBytes);
INT vendorextnWriteSection(PVOID  pContext, PUCHAR pBuffer, enum bcm_flash2x_section_val SectionVal,
			UINT offset, UINT numOfBytes, BOOLEAN bVerify);
INT vendorextnWriteSectionWithoutErase(PVOID  pContext, PUCHAR pBuffer, enum bcm_flash2x_section_val SectionVal,
			UINT offset, UINT numOfBytes);

#endif /*  */
