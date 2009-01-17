/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : func_extr.c                                           */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains function prototype.                        */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#ifndef _CFUNC_H
#define _CFUNC_H

#include "queue.h"

/* amsdu.c */
void zfDeAmsdu(zdev_t* dev, zbuf_t* buf, u16_t vap, u8_t encryMode);

/* cscanmgr.c */
void zfScanMgrInit(zdev_t* dev);
u8_t zfScanMgrScanStart(zdev_t* dev, u8_t scanType);
void zfScanMgrScanStop(zdev_t* dev, u8_t scanType);
void zfScanMgrScanAck(zdev_t* dev);

/* cpsmgr.c */
void zfPowerSavingMgrInit(zdev_t* dev);
void zfPowerSavingMgrSetMode(zdev_t* dev, u8_t mode);
void zfPowerSavingMgrMain(zdev_t* dev);
void zfPowerSavingMgrWakeup(zdev_t* dev);
u8_t zfPowerSavingMgrIsSleeping(zdev_t *dev);
void zfPowerSavingMgrProcessBeacon(zdev_t* dev, zbuf_t* buf);
void zfPowerSavingMgrAtimWinExpired(zdev_t* dev);
void zfPowerSavingMgrConnectNotify(zdev_t *dev);
void zfPowerSavingMgrPreTBTTInterrupt(zdev_t *dev);

/* ccmd.c */
u16_t zfWlanEnable(zdev_t* dev);

/* cfunc.c */
u8_t zfQueryOppositeRate(zdev_t* dev, u8_t dst_mac[6], u8_t frameType);
void zfCopyToIntTxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* src,
                         u16_t offset, u16_t length);
void zfCopyToRxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* src,
                      u16_t offset, u16_t length);
void zfCopyFromIntTxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* dst,
                           u16_t offset, u16_t length);
void zfCopyFromRxBuffer(zdev_t* dev, zbuf_t* buf, u8_t* dst,
                        u16_t offset, u16_t length);
void zfMemoryCopy(u8_t* dst, u8_t* src, u16_t length);
void zfMemoryMove(u8_t* dst, u8_t* src, u16_t length);
void zfZeroMemory(u8_t* va, u16_t length);
u8_t zfMemoryIsEqual(u8_t* m1, u8_t* m2, u16_t length);
u8_t zfRxBufferEqualToStr(zdev_t* dev, zbuf_t* buf, const u8_t* str,
                          u16_t offset, u16_t length);
void zfTxBufferCopy(zdev_t*dev, zbuf_t* dst, zbuf_t* src,
                    u16_t dstOffset, u16_t srcOffset, u16_t length);
void zfRxBufferCopy(zdev_t*dev, zbuf_t* dst, zbuf_t* src,
                    u16_t dstOffset, u16_t srcOffset, u16_t length);

void zfCollectHWTally(zdev_t*dev, u32_t* rsp, u8_t id);
void zfTimerInit(zdev_t* dev);
u16_t zfTimerSchedule(zdev_t* dev, u16_t event, u32_t tick);
u16_t zfTimerCancel(zdev_t* dev, u16_t event);
void zfTimerClear(zdev_t* dev);
u16_t zfTimerCheckAndHandle(zdev_t* dev);
void zfProcessEvent(zdev_t* dev, u16_t* eventArray, u8_t eventCount);

void zfBssInfoCreate(zdev_t* dev);
void zfBssInfoDestroy(zdev_t* dev);

struct zsBssInfo* zfBssInfoAllocate(zdev_t* dev);
void zfBssInfoFree(zdev_t* dev, struct zsBssInfo* pBssInfo);
void zfBssInfoReorderList(zdev_t* dev);
void zfBssInfoInsertToList(zdev_t* dev, struct zsBssInfo* pBssInfo);
void zfBssInfoRemoveFromList(zdev_t* dev, struct zsBssInfo* pBssInfo);
void zfBssInfoRefresh(zdev_t* dev, u16_t mode);
void zfCoreSetFrequencyComplete(zdev_t* dev);
void zfCoreSetFrequency(zdev_t* dev, u16_t frequency);
void zfCoreSetFrequencyV2(zdev_t* dev, u16_t frequency,
        zfpFreqChangeCompleteCb cb);
void zfCoreSetFrequencyEx(zdev_t* dev, u16_t frequency, u8_t bw40,
        u8_t extOffset, zfpFreqChangeCompleteCb cb);
void zfCoreSetFrequencyExV2(zdev_t* dev, u16_t frequency, u8_t bw40,
        u8_t extOffset, zfpFreqChangeCompleteCb cb, u8_t forceSetFreq);
void zfReSetCurrentFrequency(zdev_t* dev);
u32_t zfCoreSetKey(zdev_t* dev, u8_t user, u8_t keyId, u8_t type,
        u16_t* mac, u32_t* key);
void zfCoreSetKeyComplete(zdev_t* dev);
void zfCoreReinit(zdev_t* dev);
void zfCoreMacAddressNotify(zdev_t* dev, u8_t *addr);
void zfCoreSetIsoName(zdev_t* dev, u8_t* isoName);
void zfGenerateRandomBSSID(zdev_t* dev, u8_t *MACAddr, u8_t *BSSID);
void zfCoreHalInitComplete(zdev_t* dev);

u16_t zfFindCleanFrequency(zdev_t* dev, u32_t adhocMode);
u16_t zfFindMinimumUtilizationChannelIndex(zdev_t* dev, u16_t* array, u16_t count);
u8_t zfCompareWithBssid(zdev_t* dev, u16_t* bssid);

/* chb.c */
void zfDumpBssList(zdev_t* dev);


u16_t zfIssueCmd(zdev_t* dev, u32_t* cmd, u16_t cmdLen, u16_t src, u8_t* buf);


/* cic.c */
void zfUpdateBssid(zdev_t* dev, u8_t* bssid);
void zfResetSupportRate(zdev_t* dev, u8_t type);
void zfUpdateSupportRate(zdev_t* dev, u8_t* rateArray);
u8_t zfIsGOnlyMode(zdev_t* dev, u16_t  frequency, u8_t* rateArray);
void zfGatherBMode(zdev_t* dev, u8_t* rateArray, u8_t* extrateArray);
u8_t zfPSDeviceSleep(zdev_t* dev);
u16_t zfGetRandomNumber(zdev_t* dev, u16_t initValue);
void zfCoreEvent(zdev_t* dev, u16_t event, u8_t* rsp);
void zfBeaconCfgInterrupt(zdev_t* dev, u8_t* rsp);
void zfEndOfAtimWindowInterrupt(zdev_t* dev);

/* cinit.c */
u16_t zfTxGenWlanHeader(zdev_t* dev, zbuf_t* buf, u16_t* header, u16_t seq,
                        u8_t flag, u16_t plusLen, u16_t minusLen, u16_t port,
                        u16_t* da, u16_t* sa, u8_t up, u16_t *micLen,
                        u16_t* snap, u16_t snapLen, struct aggControl *aggControl);
u16_t zfTxGenMmHeader(zdev_t* dev, u8_t frameType, u16_t* dst,
        u16_t* header, u16_t len, zbuf_t* buf, u16_t vap, u8_t encrypt);
void zfInitMacApMode(zdev_t* dev);
u16_t zfChGetNextChannel(zdev_t* dev, u16_t frequency, u8_t* pbPassive);
u16_t zfChGetFirstChannel(zdev_t* dev, u8_t* pbPassive);
u16_t zfChGetFirst2GhzChannel(zdev_t* dev);
u16_t zfChGetFirst5GhzChannel(zdev_t* dev);
u16_t zfChGetLastChannel(zdev_t* dev, u8_t* pbPassive);
u16_t zfChGetLast5GhzChannel(zdev_t* dev);
u16_t zfChNumToFreq(zdev_t* dev, u8_t ch, u8_t freqBand);
u8_t zfChFreqToNum(u16_t freq, u8_t* bIs5GBand);

/* cmm.c */
void zfProcessManagement(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo); //CWYang(m)
void zfSendMmFrame(zdev_t* dev, u8_t frameType, u16_t* dst,
                   u32_t p1, u32_t p2, u32_t p3);
u16_t zfFindElement(zdev_t* dev, zbuf_t* buf, u8_t eid);
u16_t zfFindWifiElement(zdev_t* dev, zbuf_t* buf, u8_t type, u8_t subtype);
u16_t zfFindSuperGElement(zdev_t* dev, zbuf_t* buf, u8_t type);
u16_t zfFindXRElement(zdev_t* dev, zbuf_t* buf, u8_t type);
u16_t zfRemoveElement(zdev_t* dev, u8_t* buf, u16_t size, u8_t eid);
u16_t zfUpdateElement(zdev_t* dev, u8_t* buf, u16_t size, u8_t* updateeid);
void zfProcessProbeReq(zdev_t* dev, zbuf_t* buf, u16_t* src);
void zfProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo);
u16_t zfSendProbeReq(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t bWithSSID);
u16_t zfMmAddIeSupportRate(zdev_t* dev, zbuf_t* buf,
                           u16_t offset, u8_t eid, u8_t rateSet);
u16_t zfMmAddIeDs(zdev_t* dev, zbuf_t* buf, u16_t offset);
u16_t zfMmAddIeErp(zdev_t* dev, zbuf_t* buf, u16_t offset);
void zfUpdateDefaultQosParameter(zdev_t* dev, u8_t mode);
u16_t zfMmAddIeWpa(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t apId);
u16_t zfMmAddHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset); //CWYang(+)
u16_t zfMmAddPreNHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset);
u16_t zfMmAddExtendedHTCapability(zdev_t* dev, zbuf_t* buf, u16_t offset); //CWYang(+)
u16_t zfFindATHExtCap(zdev_t* dev, zbuf_t* buf, u8_t type, u8_t subtype);
u16_t zfFindBrdcmMrvlRlnkExtCap(zdev_t* dev, zbuf_t* buf);
u16_t zfFindMarvelExtCap(zdev_t* dev, zbuf_t* buf);
u16_t zfFindBroadcomExtCap(zdev_t* dev, zbuf_t* buf);
u16_t zfFindRlnkExtCap(zdev_t* dev, zbuf_t* buf);

/* cmmap.c */
void zfMmApTimeTick(zdev_t* dev);
void zfApAgingSta(zdev_t* dev);
u16_t zfApAddSta(zdev_t* dev, u16_t* addr, u16_t state, u16_t apId, u8_t type,
                 u8_t qosType, u8_t qosInfo);
void zfApProtctionMonitor(zdev_t* dev);
void zfApProcessBeacon(zdev_t* dev, zbuf_t* buf);
void zfApProcessAuth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfApProcessAsocReq(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfApProcessAsocRsp(zdev_t* dev, zbuf_t* buf);
void zfApProcessDeauth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfApProcessDisasoc(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfApProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo);
void zfApStoreAsocReqIe(zdev_t* dev, zbuf_t* buf, u16_t aid);
u16_t zfApAddIeSsid(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t vap);
void zfApSendBeacon(zdev_t* dev);
u16_t zfApGetSTAInfo(zdev_t* dev, u16_t* addr, u16_t* state, u8_t* vap);
u16_t zfIntrabssForward(zdev_t* dev, zbuf_t* buf, u8_t srcVap);
u16_t zfApBufferPsFrame(zdev_t* dev, zbuf_t* buf, u16_t port);
void zfApInitStaTbl(zdev_t* dev);
void zfApGetStaTxRateAndQosType(zdev_t* dev, u16_t* addr, u32_t* phyCtrl,
                                u8_t* qosType, u16_t* rcProbingFlag);
void zfApGetStaQosType(zdev_t* dev, u16_t* addr, u8_t* qosType);
void zfApSetStaTxRate(zdev_t* dev, u16_t* addr, u32_t phyCtrl);
struct zsMicVar* zfApGetRxMicKey(zdev_t* dev, zbuf_t* buf);
struct zsMicVar* zfApGetTxMicKey(zdev_t* dev, zbuf_t* buf, u8_t* qosType);
u16_t zfApAddIeWmePara(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t vap);
u16_t zfApUpdatePsBit(zdev_t* dev, zbuf_t* buf, u8_t* vap, u8_t* uapsdTrig);
void zfApProcessPsPoll(zdev_t* dev, zbuf_t* buf);
u16_t zfApFindSta(zdev_t* dev, u16_t* addr);
void zfApGetStaEncryType(zdev_t* dev, u16_t* addr, u8_t* encryType);
void zfApGetStaWpaIv(zdev_t* dev, u16_t* addr, u16_t* iv16, u32_t* iv32);
void zfApSetStaWpaIv(zdev_t* dev, u16_t* addr, u16_t iv16, u32_t iv32);
void zfApClearStaKey(zdev_t* dev, u16_t* addr);
#ifdef ZM_ENABLE_CENC
void zfApGetStaCencIvAndKeyIdx(zdev_t* dev, u16_t* addr, u32_t *iv,
        u8_t *keyIdx);
void zfApSetStaCencIv(zdev_t* dev, u16_t* addr, u32_t *iv);
#endif //ZM_ENABLE_CENC
void zfApSetProtectionMode(zdev_t* dev, u16_t mode);
void zfApFlushBufferedPsFrame(zdev_t* dev);
void zfApSendFailure(zdev_t* dev, u8_t* addr);
u8_t zfApRemoveFromPsQueue(zdev_t* dev, u16_t id, u16_t* src);
void zfApProcessAction(zdev_t* dev, zbuf_t* buf);
/* cmmsta.c */
void zfMmStaTimeTick(zdev_t* dev);
void zfReWriteBeaconStartAddress(zdev_t* dev);  // Mxzeng
void zfStaProcessBeacon(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo); //CWYang(m)
void zfStaProcessAuth(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfStaProcessAsocReq(zdev_t* dev, zbuf_t* buf, u16_t* src, u16_t apId);
void zfStaProcessAsocRsp(zdev_t* dev, zbuf_t* buf);
void zfStaProcessDeauth(zdev_t* dev, zbuf_t* buf);
void zfStaProcessDisasoc(zdev_t* dev, zbuf_t* buf);
void zfStaProcessProbeRsp(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* AddInfo);
void zfStaProcessAtim(zdev_t* dev, zbuf_t* buf);
void zfStaStoreAsocRspIe(zdev_t* dev, zbuf_t* buf);
void zfStaChannelManagement(zdev_t* dev, u8_t scan);
void zfIbssConnectNetwork(zdev_t* dev);
void zfInfraConnectNetwork(zdev_t* dev);
u8_t zfCheckAuthentication(zdev_t* dev, struct zsBssInfo* pBssInfo);
u8_t zfChangeAdapterState(zdev_t* dev, u8_t newState);
u16_t zfStaAddIeSsid(zdev_t* dev, zbuf_t* buf, u16_t offset);
u16_t zfStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType);
u16_t zfStaAddIeIbss(zdev_t* dev, zbuf_t* buf, u16_t offset);
void zfStaStartConnect(zdev_t* dev, u8_t bIsSharedKey);
u8_t zfStaIsConnected(zdev_t* dev);
u8_t zfStaIsConnecting(zdev_t* dev);
u8_t zfStaIsDisconnect(zdev_t* dev);
void zfStaSendBeacon(zdev_t* dev);
void zfSendNullData(zdev_t* dev, u8_t type);
void zfSendPSPoll(zdev_t* dev);
void zfSendBA(zdev_t* dev, u16_t start_seq, u8_t *bitmap);
void zdRateInfoCountTx(zdev_t* dev, u16_t* macAddr);
struct zsMicVar* zfStaGetRxMicKey(zdev_t* dev, zbuf_t* buf);
struct zsMicVar* zfStaGetTxMicKey(zdev_t* dev, zbuf_t* buf);
u16_t zfStaRxValidateFrame(zdev_t* dev, zbuf_t* buf);
void zfStaMicFailureHandling(zdev_t* dev, zbuf_t* buf);
u8_t zfStaBlockWlanScan(zdev_t* dev);
void zfStaIbssPSCheckState(zdev_t* dev, zbuf_t* buf);
u8_t zfStaIbssPSQueueData(zdev_t* dev, zbuf_t* buf);
void zfStaIbssPSSend(zdev_t* dev);
void zfStaResetStatus(zdev_t* dev, u8_t bInit);
u16_t zfStaAddIeWmeInfo(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t qosInfo);
void zfInitPartnerNotifyEvent(zdev_t* dev, zbuf_t* buf, struct zsPartnerNotifyEvent *event);
void zfStaInitOppositeInfo(zdev_t* dev);
void zfStaIbssMonitoring(zdev_t* dev, u8_t reset);
struct zsBssInfo* zfStaFindBssInfo(zdev_t* dev, zbuf_t* buf, struct zsWlanProbeRspFrameHeader *pProbeRspHeader);
u8_t zfStaInitBssInfo(zdev_t* dev, zbuf_t* buf,
        struct zsWlanProbeRspFrameHeader *pProbeRspHeader,
        struct zsBssInfo* pBssInfo, struct zsAdditionInfo* AddInfo, u8_t type);
s8_t zfStaFindFreeOpposite(zdev_t* dev, u16_t *sa, int *pFoundIdx);
s8_t zfStaFindOppositeByMACAddr(zdev_t* dev, u16_t *sa, u8_t *pFoundIdx);
void zfStaRefreshBlockList(zdev_t* dev, u16_t flushFlag);
void zfStaConnectFail(zdev_t* dev, u16_t reason, u16_t* bssid, u8_t weight);
void zfStaGetTxRate(zdev_t* dev, u16_t* macAddr, u32_t* phyCtrl,
        u16_t* rcProbingFlag);
u16_t zfStaProcessAction(zdev_t* dev, zbuf_t* buf);
struct zsTkipSeed* zfStaGetRxSeed(zdev_t* dev, zbuf_t* buf);
#ifdef ZM_ENABLE_CENC
/* CENC */
u16_t zfStaAddIeCenc(zdev_t* dev, zbuf_t* buf, u16_t offset);
#endif //ZM_ENABLE_CENC
void zfStaEnableSWEncryption(zdev_t *dev, u8_t value);
void zfStaDisableSWEncryption(zdev_t *dev);
u16_t zfComputeBssInfoWeightValue(zdev_t *dev, u8_t isBMode, u8_t isHT, u8_t isHT40, u8_t signalStrength);
u16_t zfStaAddIbssAdditionalIE(zdev_t* dev, zbuf_t* buf, u16_t offset);

/* ctkip.c */
void zfTkipInit(u8_t* key, u8_t* ta, struct zsTkipSeed* pSeed, u8_t* initIv);
void zfMicSetKey(u8_t* key, struct zsMicVar* pMic);
void zfMicAppendByte(u8_t b, struct zsMicVar* pMic);
void zfMicClear(struct zsMicVar* pMic);
void zfMicAppendTxBuf(zdev_t* dev, zbuf_t* buf, u8_t* da, u8_t* sa,
                      u16_t removeLen, u8_t* mic);
u8_t zfMicRxVerify(zdev_t* dev, zbuf_t* buf);
void zfMicGetMic(u8_t* dst, struct zsMicVar* pMic);
void zfCalTxMic(zdev_t *dev, zbuf_t *buf, u8_t *snap, u16_t snapLen, u16_t offset, u16_t *da, u16_t *sa, u8_t up, u8_t *mic);
void zfTKIPEncrypt(zdev_t *dev, zbuf_t *buf, u8_t *snap, u16_t snapLen, u16_t offset, u8_t keyLen, u8_t* key, u32_t* icv);
u16_t zfTKIPDecrypt(zdev_t *dev, zbuf_t *buf, u16_t offset, u8_t keyLen, u8_t* key);
void zfTkipGetseeds(u16_t iv16, u8_t *RC4Key, struct zsTkipSeed *Seed);
u8_t zfTkipPhase1KeyMix(u32_t iv32, struct zsTkipSeed* pSeed);
u8_t zfTkipPhase2KeyMix(u16_t iv16, struct zsTkipSeed* pSeed);
void zfWEPEncrypt(zdev_t *dev, zbuf_t *buf, u8_t *snap, u16_t snapLen, u16_t offset, u8_t keyLen, u8_t* WepKey, u8_t *iv);
u16_t zfWEPDecrypt(zdev_t *dev, zbuf_t *buf, u16_t offset, u8_t keyLen, u8_t* WepKey, u8_t *iv);

/* ctxrx.c */
u16_t zfSend80211Frame(zdev_t* dev, zbuf_t* buf);
void zfIsrPciTxComp(zdev_t* dev);
void zfTxPciDmaStart(zdev_t* dev);
u16_t zfTxPortControl(zdev_t* dev, zbuf_t* buf, u16_t port);
u16_t zfTxSendEth(zdev_t* dev, zbuf_t* buf, u16_t port,
                  u16_t bufType, u16_t flag);
u16_t zfTxGenWlanTail(zdev_t* dev, zbuf_t* buf, u16_t* snap, u16_t snaplen,
                      u16_t* mic);
u16_t zfTxGenWlanSnap(zdev_t* dev, zbuf_t* buf, u16_t* snap, u16_t* snaplen);
void zfTxGetIpTosAndFrag(zdev_t* dev, zbuf_t* buf, u8_t* up, u16_t* fragOff);
u16_t zfPutVtxq(zdev_t* dev, zbuf_t* buf);
void zfPushVtxq(zdev_t* dev);
u8_t zfIsVtxqEmpty(zdev_t* dev);
u16_t zfGetSeqCtrl(zdev_t* dev, zbuf_t* buf, u16_t offset);
u8_t zfGetFragNo(zdev_t* dev, zbuf_t* buf);
void zfShowRxEAPOL(zdev_t* dev, zbuf_t* buf, u16_t offset);
void zfShowTxEAPOL(zdev_t* dev, zbuf_t* buf, u16_t offset);
void zfCoreRecv(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
u16_t zfPutVmmq(zdev_t* dev, zbuf_t* buf);
void zfFlushVtxq(zdev_t* dev);
void zfAgingDefragList(zdev_t* dev, u16_t flushFlag);

void zfLed100msCtrl(zdev_t* dev);
void zf80211FrameSend(zdev_t* dev, zbuf_t* buf, u16_t* header, u16_t snapLen,
                           u16_t* da, u16_t* sa, u8_t up, u16_t headerLen, u16_t* snap,
                           u16_t* tail, u16_t tailLen, u16_t offset, u16_t bufType,
                           u8_t ac, u8_t keyIdx);
void zfCheckIsRIFSFrame(zdev_t* dev, zbuf_t* buf, u16_t frameSubType);

/* queue.c */
struct zsQueue* zfQueueCreate(zdev_t* dev, u16_t size);
void zfQueueDestroy(zdev_t* dev, struct zsQueue* q);
u16_t zfQueuePutNcs(zdev_t* dev, struct zsQueue* q, zbuf_t* buf, u32_t tick);
u16_t zfQueuePut(zdev_t* dev, struct zsQueue* q, zbuf_t* buf, u32_t tick);
zbuf_t* zfQueueGet(zdev_t* dev, struct zsQueue* q);
zbuf_t* zfQueueGetWithMac(zdev_t* dev, struct zsQueue* q, u8_t* addr, u8_t* mb);
void zfQueueFlush(zdev_t* dev, struct zsQueue* q);
void zfQueueAge(zdev_t* dev, struct zsQueue* q, u32_t tick, u32_t msAge);
void zfQueueGenerateUapsdTim(zdev_t* dev, struct zsQueue* q,
        u8_t* uniBitMap, u16_t* highestByte);

/* hpmain.c */
u16_t zfHpInit(zdev_t* dev, u32_t frequency);
u16_t zfHpRelease(zdev_t* dev);
void zfHpSetFrequencyEx(zdev_t* dev, u32_t frequency, u8_t bw40,
        u8_t extOffset, u8_t initRF);
u16_t zfHpStartRecv(zdev_t* dev);
u16_t zfHpStopRecv(zdev_t* dev);
u16_t zfHpResetKeyCache(zdev_t* dev);
u16_t zfHpSetApStaMode(zdev_t* dev, u8_t mode);
u16_t zfHpSetBssid(zdev_t* dev, u8_t* bssid);
u16_t zfHpSetSnifferMode(zdev_t* dev, u16_t on);
u8_t zfHpUpdateQosParameter(zdev_t* dev, u16_t* cwminTbl, u16_t* cwmaxTbl,
        u16_t* aifsTbl, u16_t* txopTbl);
void zfHpSetAtimWindow(zdev_t* dev, u16_t atimWin);
void zfHpEnableBeacon(zdev_t* dev, u16_t mode, u16_t bcnInterval, u16_t dtim, u8_t enableAtim);
void zfHpDisableBeacon(zdev_t* dev);
void zfHpSetBasicRateSet(zdev_t* dev, u16_t bRateBasic, u16_t gRateBasic);
void zfHpSetRTSCTSRate(zdev_t* dev, u32_t rate);
void zfHpSetMacAddress(zdev_t* dev, u16_t* macAddr, u16_t macAddrId);
u32_t zfHpGetMacAddress(zdev_t* dev);
u32_t zfHpGetTransmitPower(zdev_t* dev);
void zfHpSetMulticastList(zdev_t* dev, u8_t size, u8_t* pList, u8_t bAllMulticast);

u16_t zfHpRemoveKey(zdev_t* dev, u16_t user);
u32_t zfHpSetKey(zdev_t* dev, u8_t user, u8_t keyId, u8_t type,
        u16_t* mac, u32_t* key);
//u32_t zfHpSetStaPairwiseKey(zdev_t* dev, u16_t* apMacAddr, u8_t type,
//        u32_t* key, u32_t* micKey);
//u32_t zfHpSetStaGroupKey(zdev_t* dev, u16_t* apMacAddr, u8_t type,
//        u32_t* key, u32_t* micKey);
u32_t zfHpSetApPairwiseKey(zdev_t* dev, u16_t* staMacAddr, u8_t type,
        u32_t* key, u32_t* micKey, u16_t staAid);
u32_t zfHpSetApGroupKey(zdev_t* dev, u16_t* apMacAddr, u8_t type,
        u32_t* key, u32_t* micKey, u16_t vapId);
u32_t zfHpSetDefaultKey(zdev_t* dev, u8_t keyId, u8_t type, u32_t* key, u32_t* micKey);
u32_t zfHpSetPerUserKey(zdev_t* dev, u8_t user, u8_t keyId, u8_t* mac, u8_t type, u32_t* key, u32_t* micKey);

void zfHpSendBeacon(zdev_t* dev, zbuf_t* buf, u16_t len);
u16_t zfHpGetPayloadLen(zdev_t* dev,
                        zbuf_t* buf,
                        u16_t len,
                        u16_t plcpHdrLen,
                        u32_t *rxMT,
                        u32_t *rxMCS,
                        u32_t *rxBW,
                        u32_t *rxSG
                        );
u32_t zfHpGetFreeTxdCount(zdev_t* dev);
u32_t zfHpGetMaxTxdCount(zdev_t* dev);
u16_t zfHpSend(zdev_t* dev, u16_t* header, u16_t headerLen,
        u16_t* snap, u16_t snapLen, u16_t* tail, u16_t tailLen, zbuf_t* buf,
        u16_t offset, u16_t bufType, u8_t ac, u8_t keyIdx);
void zfHpGetRegulationTablefromRegionCode(zdev_t* dev, u16_t regionCode);
void zfHpGetRegulationTablefromCountry(zdev_t* dev, u16_t CountryCode);
u8_t zfHpGetRegulationTablefromISO(zdev_t* dev, u8_t *countryInfo, u8_t length);
const char* zfHpGetisoNamefromregionCode(zdev_t* dev, u16_t regionCode);
u16_t zfHpGetRegionCodeFromIsoName(zdev_t* dev, u8_t *countryIsoName);
u8_t zfHpGetRegulatoryDomain(zdev_t* dev);
void zfHpLedCtrl(zdev_t* dev, u16_t ledId, u8_t mode);
u16_t zfHpResetTxRx(zdev_t* dev);
u16_t zfHpDeleteAllowChannel(zdev_t* dev, u16_t freq);
u16_t zfHpAddAllowChannel(zdev_t* dev, u16_t freq);
u32_t zfHpCwmUpdate(zdev_t* dev);
u32_t zfHpAniUpdate(zdev_t* dev);
u32_t zfHpAniUpdateRssi(zdev_t* dev, u8_t rssi);
void zfHpAniAttach(zdev_t* dev);
void zfHpAniArPoll(zdev_t* dev, u32_t listenTime, u32_t phyCnt1, u32_t phyCnt2);
void zfHpHeartBeat(zdev_t* dev);
void zfHpPowerSaveSetState(zdev_t* dev, u8_t psState);
void zfHpPowerSaveSetMode(zdev_t* dev, u8_t staMode, u8_t psMode, u16_t bcnInterval);
u16_t zfHpIsDfsChannel(zdev_t* dev, u16_t freq);
u16_t zfHpIsDfsChannelNCS(zdev_t* dev, u16_t freq);
u16_t zfHpFindFirstNonDfsChannel(zdev_t* dev, u16_t aBand);
u16_t zfHpIsAllowedChannel(zdev_t* dev, u16_t freq);
void zfHpDisableDfsChannel(zdev_t* dev, u8_t disableFlag);
void zfHpSetTTSIFSTime(zdev_t* dev, u8_t sifs_time);

void zfHpQueryMonHalRxInfo(zdev_t* dev, u8_t *monHalRxInfo);

void zfDumpSSID(u8_t length, u8_t *value);
void zfHpSetAggPktNum(zdev_t* dev, u32_t num);
void zfHpSetMPDUDensity(zdev_t* dev, u8_t density);
void zfHpSetSlotTime(zdev_t* dev, u8_t type);
void zfHpSetSlotTimeRegister(zdev_t* dev, u8_t type);
void zfHpSetRifs(zdev_t* dev, u8_t ht_enable, u8_t ht2040, u8_t g_mode);
void zfHpBeginSiteSurvey(zdev_t* dev, u8_t status);
void zfHpFinishSiteSurvey(zdev_t* dev, u8_t status);
u16_t zfHpEnableHwRetry(zdev_t* dev);
u16_t zfHpDisableHwRetry(zdev_t* dev);
void zfHpSWDecrypt(zdev_t* dev, u8_t enable);
void zfHpSWEncrypt(zdev_t* dev, u8_t enable);
u32_t zfHpCapability(zdev_t* dev);
void zfHpSetRollCallTable(zdev_t* dev);
u8_t zfHpregulatoryDomain(zdev_t* dev);
u16_t zfStaAddIePowerCap(zdev_t* dev, zbuf_t* buf, u16_t offset);
u8_t zfHpGetMaxTxPower(zdev_t* dev);
u8_t zfHpGetMinTxPower(zdev_t* dev);
u16_t zfStaAddIeSupportCh(zdev_t* dev, zbuf_t* buf, u16_t offset);
void zfHpEnableRifs(zdev_t* dev, u8_t mode24g, u8_t modeHt, u8_t modeHt2040);
void zfHpDisableRifs(zdev_t* dev);
u16_t zfHpUsbReset(zdev_t* dev);


#endif /* #ifndef _CFUNC_H */
