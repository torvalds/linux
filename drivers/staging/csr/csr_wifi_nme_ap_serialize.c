/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_pmem.h"
#include "csr_msgconv.h"
#include "csr_unicode.h"

#ifdef CSR_WIFI_NME_ENABLE
#ifdef CSR_WIFI_AP_ENABLE

#include "csr_wifi_nme_ap_prim.h"
#include "csr_wifi_nme_ap_serialize.h"

void CsrWifiNmeApPfree(void *ptr)
{
    CsrPmemFree(ptr);
}


CsrSize CsrWifiNmeApConfigSetReqSizeof(void *msg)
{
    CsrWifiNmeApConfigSetReq *primitive = (CsrWifiNmeApConfigSetReq *) msg;
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 104) */
    bufferSize += 2;  /* u16 primitive->apConfig.apGroupkeyTimeout */
    bufferSize += 1;  /* CsrBool primitive->apConfig.apStrictGtkRekey */
    bufferSize += 2;  /* u16 primitive->apConfig.apGmkTimeout */
    bufferSize += 2;  /* u16 primitive->apConfig.apResponseTimeout */
    bufferSize += 1;  /* u8 primitive->apConfig.apRetransLimit */
    bufferSize += 1;  /* CsrWifiSmeApPhySupportMask primitive->apMacConfig.phySupportedBitmap */
    bufferSize += 2;  /* u16 primitive->apMacConfig.beaconInterval */
    bufferSize += 1;  /* u8 primitive->apMacConfig.dtimPeriod */
    bufferSize += 2;  /* u16 primitive->apMacConfig.maxListenInterval */
    bufferSize += 1;  /* u8 primitive->apMacConfig.supportedRatesCount */
    bufferSize += 20; /* u8 primitive->apMacConfig.supportedRates[20] */
    bufferSize += 1;  /* CsrWifiSmePreambleType primitive->apMacConfig.preamble */
    bufferSize += 1;  /* CsrBool primitive->apMacConfig.shortSlotTimeEnabled */
    bufferSize += 1;  /* CsrWifiSmeCtsProtectionType primitive->apMacConfig.ctsProtectionType */
    bufferSize += 1;  /* CsrBool primitive->apMacConfig.wmmEnabled */
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApParams[i2].cwMin */
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApParams[i2].cwMax */
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApParams[i2].aifs */
            bufferSize += 2; /* u16 primitive->apMacConfig.wmmApParams[i2].txopLimit */
            bufferSize += 1; /* CsrBool primitive->apMacConfig.wmmApParams[i2].admissionControlMandatory */
        }
    }
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApBcParams[i2].cwMin */
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApBcParams[i2].cwMax */
            bufferSize += 1; /* u8 primitive->apMacConfig.wmmApBcParams[i2].aifs */
            bufferSize += 2; /* u16 primitive->apMacConfig.wmmApBcParams[i2].txopLimit */
            bufferSize += 1; /* CsrBool primitive->apMacConfig.wmmApBcParams[i2].admissionControlMandatory */
        }
    }
    bufferSize += 1;         /* CsrWifiSmeApAccessType primitive->apMacConfig.accessType */
    bufferSize += 1;         /* u8 primitive->apMacConfig.macAddressListCount */
    {
        u16 i2;
        for (i2 = 0; i2 < primitive->apMacConfig.macAddressListCount; i2++)
        {
            bufferSize += 6; /* u8 primitive->apMacConfig.macAddressList[i2].a[6] */
        }
    }
    bufferSize += 1;         /* CsrBool primitive->apMacConfig.apHtParams.greenfieldSupported */
    bufferSize += 1;         /* CsrBool primitive->apMacConfig.apHtParams.shortGi20MHz */
    bufferSize += 1;         /* u8 primitive->apMacConfig.apHtParams.rxStbc */
    bufferSize += 1;         /* CsrBool primitive->apMacConfig.apHtParams.rifsModeAllowed */
    bufferSize += 1;         /* u8 primitive->apMacConfig.apHtParams.htProtection */
    bufferSize += 1;         /* CsrBool primitive->apMacConfig.apHtParams.dualCtsProtection */
    return bufferSize;
}


u8* CsrWifiNmeApConfigSetReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApConfigSetReq *primitive = (CsrWifiNmeApConfigSetReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->apConfig.apGroupkeyTimeout);
    CsrUint8Ser(ptr, len, (u8) primitive->apConfig.apStrictGtkRekey);
    CsrUint16Ser(ptr, len, (u16) primitive->apConfig.apGmkTimeout);
    CsrUint16Ser(ptr, len, (u16) primitive->apConfig.apResponseTimeout);
    CsrUint8Ser(ptr, len, (u8) primitive->apConfig.apRetransLimit);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.phySupportedBitmap);
    CsrUint16Ser(ptr, len, (u16) primitive->apMacConfig.beaconInterval);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.dtimPeriod);
    CsrUint16Ser(ptr, len, (u16) primitive->apMacConfig.maxListenInterval);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.supportedRatesCount);
    CsrMemCpySer(ptr, len, (const void *) primitive->apMacConfig.supportedRates, ((u16) (20)));
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.preamble);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.shortSlotTimeEnabled);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.ctsProtectionType);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmEnabled);
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApParams[i2].cwMin);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApParams[i2].cwMax);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApParams[i2].aifs);
            CsrUint16Ser(ptr, len, (u16) primitive->apMacConfig.wmmApParams[i2].txopLimit);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApParams[i2].admissionControlMandatory);
        }
    }
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApBcParams[i2].cwMin);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApBcParams[i2].cwMax);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApBcParams[i2].aifs);
            CsrUint16Ser(ptr, len, (u16) primitive->apMacConfig.wmmApBcParams[i2].txopLimit);
            CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.wmmApBcParams[i2].admissionControlMandatory);
        }
    }
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.accessType);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.macAddressListCount);
    {
        u16 i2;
        for (i2 = 0; i2 < primitive->apMacConfig.macAddressListCount; i2++)
        {
            CsrMemCpySer(ptr, len, (const void *) primitive->apMacConfig.macAddressList[i2].a, ((u16) (6)));
        }
    }
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.greenfieldSupported);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.shortGi20MHz);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.rxStbc);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.rifsModeAllowed);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.htProtection);
    CsrUint8Ser(ptr, len, (u8) primitive->apMacConfig.apHtParams.dualCtsProtection);
    return(ptr);
}


void* CsrWifiNmeApConfigSetReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApConfigSetReq *primitive = (CsrWifiNmeApConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApConfigSetReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->apConfig.apGroupkeyTimeout, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apConfig.apStrictGtkRekey, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->apConfig.apGmkTimeout, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->apConfig.apResponseTimeout, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apConfig.apRetransLimit, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.phySupportedBitmap, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->apMacConfig.beaconInterval, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.dtimPeriod, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->apMacConfig.maxListenInterval, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.supportedRatesCount, buffer, &offset);
    CsrMemCpyDes(primitive->apMacConfig.supportedRates, buffer, &offset, ((u16) (20)));
    CsrUint8Des((u8 *) &primitive->apMacConfig.preamble, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.shortSlotTimeEnabled, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.ctsProtectionType, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.wmmEnabled, buffer, &offset);
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApParams[i2].cwMin, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApParams[i2].cwMax, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApParams[i2].aifs, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->apMacConfig.wmmApParams[i2].txopLimit, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApParams[i2].admissionControlMandatory, buffer, &offset);
        }
    }
    {
        u16 i2;
        for (i2 = 0; i2 < 4; i2++)
        {
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApBcParams[i2].cwMin, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApBcParams[i2].cwMax, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApBcParams[i2].aifs, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->apMacConfig.wmmApBcParams[i2].txopLimit, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apMacConfig.wmmApBcParams[i2].admissionControlMandatory, buffer, &offset);
        }
    }
    CsrUint8Des((u8 *) &primitive->apMacConfig.accessType, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.macAddressListCount, buffer, &offset);
    primitive->apMacConfig.macAddressList = NULL;
    if (primitive->apMacConfig.macAddressListCount)
    {
        primitive->apMacConfig.macAddressList = (CsrWifiMacAddress *)CsrPmemAlloc(sizeof(CsrWifiMacAddress) * primitive->apMacConfig.macAddressListCount);
    }
    {
        u16 i2;
        for (i2 = 0; i2 < primitive->apMacConfig.macAddressListCount; i2++)
        {
            CsrMemCpyDes(primitive->apMacConfig.macAddressList[i2].a, buffer, &offset, ((u16) (6)));
        }
    }
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.greenfieldSupported, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.shortGi20MHz, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.rxStbc, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.rifsModeAllowed, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.htProtection, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apMacConfig.apHtParams.dualCtsProtection, buffer, &offset);

    return primitive;
}


void CsrWifiNmeApConfigSetReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiNmeApConfigSetReq *primitive = (CsrWifiNmeApConfigSetReq *) voidPrimitivePointer;
    CsrPmemFree(primitive->apMacConfig.macAddressList);
    CsrPmemFree(primitive);
}


CsrSize CsrWifiNmeApWpsRegisterReqSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 17) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiSmeWpsDpid primitive->selectedDevicePasswordId */
    bufferSize += 2; /* CsrWifiSmeWpsConfigType primitive->selectedConfigMethod */
    bufferSize += 8; /* u8 primitive->pin[8] */
    return bufferSize;
}


u8* CsrWifiNmeApWpsRegisterReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApWpsRegisterReq *primitive = (CsrWifiNmeApWpsRegisterReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->selectedDevicePasswordId);
    CsrUint16Ser(ptr, len, (u16) primitive->selectedConfigMethod);
    CsrMemCpySer(ptr, len, (const void *) primitive->pin, ((u16) (8)));
    return(ptr);
}


void* CsrWifiNmeApWpsRegisterReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApWpsRegisterReq *primitive = (CsrWifiNmeApWpsRegisterReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApWpsRegisterReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->selectedDevicePasswordId, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->selectedConfigMethod, buffer, &offset);
    CsrMemCpyDes(primitive->pin, buffer, &offset, ((u16) (8)));

    return primitive;
}


CsrSize CsrWifiNmeApStartReqSizeof(void *msg)
{
    CsrWifiNmeApStartReq *primitive = (CsrWifiNmeApStartReq *) msg;
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 112) */
    bufferSize += 2;  /* u16 primitive->interfaceTag */
    bufferSize += 1;  /* CsrWifiSmeApType primitive->apType */
    bufferSize += 1;  /* CsrBool primitive->cloakSsid */
    bufferSize += 32; /* u8 primitive->ssid.ssid[32] */
    bufferSize += 1;  /* u8 primitive->ssid.length */
    bufferSize += 1;  /* CsrWifiSmeRadioIF primitive->ifIndex */
    bufferSize += 1;  /* u8 primitive->channel */
    bufferSize += 1;  /* CsrWifiSmeApAuthType primitive->apCredentials.authType */
    switch (primitive->apCredentials.authType)
    {
        case CSR_WIFI_SME_AP_AUTH_TYPE_OPEN_SYSTEM:
            bufferSize += 1; /* u8 primitive->apCredentials.nmeAuthType.openSystemEmpty.empty */
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_WEP:
            bufferSize += 1; /* CsrWifiSmeWepCredentialType primitive->apCredentials.nmeAuthType.authwep.wepKeyType */
            switch (primitive->apCredentials.nmeAuthType.authwep.wepKeyType)
            {
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP128:
                    bufferSize += 1;  /* CsrWifiSmeWepAuthMode primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.wepAuthType */
                    bufferSize += 1;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.selectedWepKey */
                    bufferSize += 13; /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key1[13] */
                    bufferSize += 13; /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key2[13] */
                    bufferSize += 13; /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key3[13] */
                    bufferSize += 13; /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key4[13] */
                    break;
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP64:
                    bufferSize += 1;  /* CsrWifiSmeWepAuthMode primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.wepAuthType */
                    bufferSize += 1;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.selectedWepKey */
                    bufferSize += 5;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key1[5] */
                    bufferSize += 5;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key2[5] */
                    bufferSize += 5;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key3[5] */
                    bufferSize += 5;  /* u8 primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key4[5] */
                    break;
                default:
                    break;
            }
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL:
            bufferSize += 1; /* CsrWifiSmeApAuthSupportMask primitive->apCredentials.nmeAuthType.authTypePersonal.authSupport */
            bufferSize += 2; /* CsrWifiSmeApRsnCapabilitiesMask primitive->apCredentials.nmeAuthType.authTypePersonal.rsnCapabilities */
            bufferSize += 2; /* CsrWifiSmeApWapiCapabilitiesMask primitive->apCredentials.nmeAuthType.authTypePersonal.wapiCapabilities */
            bufferSize += 1; /* CsrWifiNmeApPersCredentialType primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase */
            switch (primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase)
            {
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PSK:
                    bufferSize += 2;                                                                                                                                                                                                                      /* u16 primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.encryptionMode */
                    bufferSize += 32;                                                                                                                                                                                                                     /* u8 primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.psk[32] */
                    break;
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE:
                    bufferSize += 2;                                                                                                                                                                                                                      /* u16 primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.encryptionMode */
                    bufferSize += (primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase?CsrStrLen(primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase) : 0) + 1; /* CsrCharString* primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase (0 byte len + 1 for NULL Term) */
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    bufferSize += 1; /* u8 primitive->maxConnections */
    bufferSize += 1; /* CsrWifiSmeP2pGroupCapabilityMask primitive->p2pGoParam.groupCapability */
    bufferSize += 3; /* u8 primitive->p2pGoParam.operatingChanList.country[3] */
    bufferSize += 1; /* u8 primitive->p2pGoParam.operatingChanList.channelEntryListCount */
    {
        u16 i3;
        for (i3 = 0; i3 < primitive->p2pGoParam.operatingChanList.channelEntryListCount; i3++)
        {
            bufferSize += 1;                                                                                  /* u8 primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingClass */
            bufferSize += 1;                                                                                  /* u8 primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount */
            bufferSize += primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount; /* u8 primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel */
        }
    }
    bufferSize += 1;                                                                                          /* CsrBool primitive->p2pGoParam.opPsEnabled */
    bufferSize += 1;                                                                                          /* u8 primitive->p2pGoParam.ctWindow */
    bufferSize += 1;                                                                                          /* CsrWifiSmeP2pNoaConfigMethod primitive->p2pGoParam.noaConfigMethod */
    bufferSize += 1;                                                                                          /* CsrBool primitive->p2pGoParam.allowNoaWithNonP2pDevices */
    bufferSize += 1;                                                                                          /* CsrBool primitive->wpsEnabled */
    return bufferSize;
}


u8* CsrWifiNmeApStartReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStartReq *primitive = (CsrWifiNmeApStartReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->apType);
    CsrUint8Ser(ptr, len, (u8) primitive->cloakSsid);
    CsrMemCpySer(ptr, len, (const void *) primitive->ssid.ssid, ((u16) (32)));
    CsrUint8Ser(ptr, len, (u8) primitive->ssid.length);
    CsrUint8Ser(ptr, len, (u8) primitive->ifIndex);
    CsrUint8Ser(ptr, len, (u8) primitive->channel);
    CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.authType);
    switch (primitive->apCredentials.authType)
    {
        case CSR_WIFI_SME_AP_AUTH_TYPE_OPEN_SYSTEM:
            CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.openSystemEmpty.empty);
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_WEP:
            CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authwep.wepKeyType);
            switch (primitive->apCredentials.nmeAuthType.authwep.wepKeyType)
            {
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP128:
                    CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.wepAuthType);
                    CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.selectedWepKey);
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key1, ((u16) (13)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key2, ((u16) (13)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key3, ((u16) (13)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key4, ((u16) (13)));
                    break;
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP64:
                    CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.wepAuthType);
                    CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.selectedWepKey);
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key1, ((u16) (5)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key2, ((u16) (5)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key3, ((u16) (5)));
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key4, ((u16) (5)));
                    break;
                default:
                    break;
            }
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL:
            CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authTypePersonal.authSupport);
            CsrUint16Ser(ptr, len, (u16) primitive->apCredentials.nmeAuthType.authTypePersonal.rsnCapabilities);
            CsrUint16Ser(ptr, len, (u16) primitive->apCredentials.nmeAuthType.authTypePersonal.wapiCapabilities);
            CsrUint8Ser(ptr, len, (u8) primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase);
            switch (primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase)
            {
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PSK:
                    CsrUint16Ser(ptr, len, (u16) primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.encryptionMode);
                    CsrMemCpySer(ptr, len, (const void *) primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.psk, ((u16) (32)));
                    break;
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE:
                    CsrUint16Ser(ptr, len, (u16) primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.encryptionMode);
                    CsrCharStringSer(ptr, len, primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    CsrUint8Ser(ptr, len, (u8) primitive->maxConnections);
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.groupCapability);
    CsrMemCpySer(ptr, len, (const void *) primitive->p2pGoParam.operatingChanList.country, ((u16) (3)));
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.operatingChanList.channelEntryListCount);
    {
        u16 i3;
        for (i3 = 0; i3 < primitive->p2pGoParam.operatingChanList.channelEntryListCount; i3++)
        {
            CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingClass);
            CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount);
            if (primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount)
            {
                CsrMemCpySer(ptr, len, (const void *) primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel, ((u16) (primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount)));
            }
        }
    }
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.opPsEnabled);
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.ctWindow);
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.noaConfigMethod);
    CsrUint8Ser(ptr, len, (u8) primitive->p2pGoParam.allowNoaWithNonP2pDevices);
    CsrUint8Ser(ptr, len, (u8) primitive->wpsEnabled);
    return(ptr);
}


void* CsrWifiNmeApStartReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStartReq *primitive = (CsrWifiNmeApStartReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApStartReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apType, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->cloakSsid, buffer, &offset);
    CsrMemCpyDes(primitive->ssid.ssid, buffer, &offset, ((u16) (32)));
    CsrUint8Des((u8 *) &primitive->ssid.length, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->ifIndex, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->channel, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apCredentials.authType, buffer, &offset);
    switch (primitive->apCredentials.authType)
    {
        case CSR_WIFI_SME_AP_AUTH_TYPE_OPEN_SYSTEM:
            CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.openSystemEmpty.empty, buffer, &offset);
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_WEP:
            CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authwep.wepKeyType, buffer, &offset);
            switch (primitive->apCredentials.nmeAuthType.authwep.wepKeyType)
            {
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP128:
                    CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.wepAuthType, buffer, &offset);
                    CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.selectedWepKey, buffer, &offset);
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key1, buffer, &offset, ((u16) (13)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key2, buffer, &offset, ((u16) (13)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key3, buffer, &offset, ((u16) (13)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep128Key.key4, buffer, &offset, ((u16) (13)));
                    break;
                case CSR_WIFI_SME_CREDENTIAL_TYPE_WEP64:
                    CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.wepAuthType, buffer, &offset);
                    CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.selectedWepKey, buffer, &offset);
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key1, buffer, &offset, ((u16) (5)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key2, buffer, &offset, ((u16) (5)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key3, buffer, &offset, ((u16) (5)));
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authwep.wepCredentials.wep64Key.key4, buffer, &offset, ((u16) (5)));
                    break;
                default:
                    break;
            }
            break;
        case CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL:
            CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.authSupport, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.rsnCapabilities, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.wapiCapabilities, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase, buffer, &offset);
            switch (primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase)
            {
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PSK:
                    CsrUint16Des((u16 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.encryptionMode, buffer, &offset);
                    CsrMemCpyDes(primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.psk.psk, buffer, &offset, ((u16) (32)));
                    break;
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE:
                    CsrUint16Des((u16 *) &primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.encryptionMode, buffer, &offset);
                    CsrCharStringDes(&primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase, buffer, &offset);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    CsrUint8Des((u8 *) &primitive->maxConnections, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->p2pGoParam.groupCapability, buffer, &offset);
    CsrMemCpyDes(primitive->p2pGoParam.operatingChanList.country, buffer, &offset, ((u16) (3)));
    CsrUint8Des((u8 *) &primitive->p2pGoParam.operatingChanList.channelEntryListCount, buffer, &offset);
    primitive->p2pGoParam.operatingChanList.channelEntryList = NULL;
    if (primitive->p2pGoParam.operatingChanList.channelEntryListCount)
    {
        primitive->p2pGoParam.operatingChanList.channelEntryList = (CsrWifiSmeApP2pOperatingChanEntry *)CsrPmemAlloc(sizeof(CsrWifiSmeApP2pOperatingChanEntry) * primitive->p2pGoParam.operatingChanList.channelEntryListCount);
    }
    {
        u16 i3;
        for (i3 = 0; i3 < primitive->p2pGoParam.operatingChanList.channelEntryListCount; i3++)
        {
            CsrUint8Des((u8 *) &primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingClass, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount, buffer, &offset);
            if (primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount)
            {
                primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel = (u8 *)CsrPmemAlloc(primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount);
                CsrMemCpyDes(primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel, buffer, &offset, ((u16) (primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannelCount)));
            }
            else
            {
                primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel = NULL;
            }
        }
    }
    CsrUint8Des((u8 *) &primitive->p2pGoParam.opPsEnabled, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->p2pGoParam.ctWindow, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->p2pGoParam.noaConfigMethod, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->p2pGoParam.allowNoaWithNonP2pDevices, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->wpsEnabled, buffer, &offset);

    return primitive;
}


void CsrWifiNmeApStartReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiNmeApStartReq *primitive = (CsrWifiNmeApStartReq *) voidPrimitivePointer;
    switch (primitive->apCredentials.authType)
    {
        case CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL:
            switch (primitive->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase)
            {
                case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE:
                    CsrPmemFree(primitive->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    {
        u16 i3;
        for (i3 = 0; i3 < primitive->p2pGoParam.operatingChanList.channelEntryListCount; i3++)
        {
            CsrPmemFree(primitive->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel);
        }
    }
    CsrPmemFree(primitive->p2pGoParam.operatingChanList.channelEntryList);
    CsrPmemFree(primitive);
}


CsrSize CsrWifiNmeApWmmParamUpdateReqSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 51) */
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            bufferSize += 1; /* u8 primitive->wmmApParams[i1].cwMin */
            bufferSize += 1; /* u8 primitive->wmmApParams[i1].cwMax */
            bufferSize += 1; /* u8 primitive->wmmApParams[i1].aifs */
            bufferSize += 2; /* u16 primitive->wmmApParams[i1].txopLimit */
            bufferSize += 1; /* CsrBool primitive->wmmApParams[i1].admissionControlMandatory */
        }
    }
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            bufferSize += 1; /* u8 primitive->wmmApBcParams[i1].cwMin */
            bufferSize += 1; /* u8 primitive->wmmApBcParams[i1].cwMax */
            bufferSize += 1; /* u8 primitive->wmmApBcParams[i1].aifs */
            bufferSize += 2; /* u16 primitive->wmmApBcParams[i1].txopLimit */
            bufferSize += 1; /* CsrBool primitive->wmmApBcParams[i1].admissionControlMandatory */
        }
    }
    return bufferSize;
}


u8* CsrWifiNmeApWmmParamUpdateReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApWmmParamUpdateReq *primitive = (CsrWifiNmeApWmmParamUpdateReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApParams[i1].cwMin);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApParams[i1].cwMax);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApParams[i1].aifs);
            CsrUint16Ser(ptr, len, (u16) primitive->wmmApParams[i1].txopLimit);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApParams[i1].admissionControlMandatory);
        }
    }
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApBcParams[i1].cwMin);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApBcParams[i1].cwMax);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApBcParams[i1].aifs);
            CsrUint16Ser(ptr, len, (u16) primitive->wmmApBcParams[i1].txopLimit);
            CsrUint8Ser(ptr, len, (u8) primitive->wmmApBcParams[i1].admissionControlMandatory);
        }
    }
    return(ptr);
}


void* CsrWifiNmeApWmmParamUpdateReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApWmmParamUpdateReq *primitive = (CsrWifiNmeApWmmParamUpdateReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApWmmParamUpdateReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            CsrUint8Des((u8 *) &primitive->wmmApParams[i1].cwMin, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApParams[i1].cwMax, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApParams[i1].aifs, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->wmmApParams[i1].txopLimit, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApParams[i1].admissionControlMandatory, buffer, &offset);
        }
    }
    {
        u16 i1;
        for (i1 = 0; i1 < 4; i1++)
        {
            CsrUint8Des((u8 *) &primitive->wmmApBcParams[i1].cwMin, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApBcParams[i1].cwMax, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApBcParams[i1].aifs, buffer, &offset);
            CsrUint16Des((u16 *) &primitive->wmmApBcParams[i1].txopLimit, buffer, &offset);
            CsrUint8Des((u8 *) &primitive->wmmApBcParams[i1].admissionControlMandatory, buffer, &offset);
        }
    }

    return primitive;
}


CsrSize CsrWifiNmeApStaRemoveReqSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 12) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->staMacAddress.a[6] */
    bufferSize += 1; /* CsrBool primitive->keepBlocking */
    return bufferSize;
}


u8* CsrWifiNmeApStaRemoveReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStaRemoveReq *primitive = (CsrWifiNmeApStaRemoveReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->staMacAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->keepBlocking);
    return(ptr);
}


void* CsrWifiNmeApStaRemoveReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStaRemoveReq *primitive = (CsrWifiNmeApStaRemoveReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApStaRemoveReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->staMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->keepBlocking, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiNmeApWpsRegisterCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiNmeApWpsRegisterCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApWpsRegisterCfm *primitive = (CsrWifiNmeApWpsRegisterCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiNmeApWpsRegisterCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApWpsRegisterCfm *primitive = (CsrWifiNmeApWpsRegisterCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApWpsRegisterCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiNmeApStartCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 40) */
    bufferSize += 2;  /* u16 primitive->interfaceTag */
    bufferSize += 2;  /* CsrResult primitive->status */
    bufferSize += 32; /* u8 primitive->ssid.ssid[32] */
    bufferSize += 1;  /* u8 primitive->ssid.length */
    return bufferSize;
}


u8* CsrWifiNmeApStartCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStartCfm *primitive = (CsrWifiNmeApStartCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrMemCpySer(ptr, len, (const void *) primitive->ssid.ssid, ((u16) (32)));
    CsrUint8Ser(ptr, len, (u8) primitive->ssid.length);
    return(ptr);
}


void* CsrWifiNmeApStartCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStartCfm *primitive = (CsrWifiNmeApStartCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApStartCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrMemCpyDes(primitive->ssid.ssid, buffer, &offset, ((u16) (32)));
    CsrUint8Des((u8 *) &primitive->ssid.length, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiNmeApStopCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiNmeApStopCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStopCfm *primitive = (CsrWifiNmeApStopCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiNmeApStopCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStopCfm *primitive = (CsrWifiNmeApStopCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApStopCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiNmeApStopIndSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 8) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiSmeApType primitive->apType */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiNmeApStopIndSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStopInd *primitive = (CsrWifiNmeApStopInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->apType);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiNmeApStopIndDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStopInd *primitive = (CsrWifiNmeApStopInd *) CsrPmemAlloc(sizeof(CsrWifiNmeApStopInd));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->apType, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiNmeApStationIndSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 18) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiSmeMediaStatus primitive->mediaStatus */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 6; /* u8 primitive->peerDeviceAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiNmeApStationIndSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiNmeApStationInd *primitive = (CsrWifiNmeApStationInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->mediaStatus);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrMemCpySer(ptr, len, (const void *) primitive->peerDeviceAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiNmeApStationIndDes(u8 *buffer, CsrSize length)
{
    CsrWifiNmeApStationInd *primitive = (CsrWifiNmeApStationInd *) CsrPmemAlloc(sizeof(CsrWifiNmeApStationInd));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->mediaStatus, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrMemCpyDes(primitive->peerDeviceAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


#endif /* CSR_WIFI_NME_ENABLE */
#endif /* CSR_WIFI_AP_ENABLE */
