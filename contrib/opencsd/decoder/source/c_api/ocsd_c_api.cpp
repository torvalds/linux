/*
 * \file       ocsd_c_api.cpp
 * \brief      OpenCSD : "C" API libary implementation.
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#include <cstring>

/* pull in the C++ decode library */
#include "opencsd.h"

/* C-API and wrapper objects */
#include "opencsd/c_api/opencsd_c_api.h"
#include "ocsd_c_api_obj.h"

/** MSVC2010 unwanted export workaround */
#ifdef WIN32
#if (_MSC_VER == 1600)
#include <new>
namespace std { const nothrow_t nothrow = nothrow_t(); }
#endif
#endif

/*******************************************************************************/
/* C API internal helper function declarations                                 */
/*******************************************************************************/

static ocsd_err_t ocsd_create_pkt_sink_cb(ocsd_trace_protocol_t protocol, FnDefPktDataIn pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj );
static ocsd_err_t ocsd_create_pkt_mon_cb(ocsd_trace_protocol_t protocol, FnDefPktDataMon pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj );
static ocsd_err_t ocsd_check_and_add_mem_acc_mapper(const dcd_tree_handle_t handle, DecodeTree **ppDT);

/*******************************************************************************/
/* C library data - additional data on top of the C++ library objects          */
/*******************************************************************************/

/* keep a list of interface objects for a decode tree for later disposal */
typedef struct _lib_dt_data_list {
    std::vector<ITrcTypedBase *> cb_objs;
    DefLogStrCBObj s_def_log_str_cb;
} lib_dt_data_list;

/* map lists to handles */
static std::map<dcd_tree_handle_t, lib_dt_data_list *> s_data_map;

/*******************************************************************************/
/* C API functions                                                             */
/*******************************************************************************/

/** Get Library version. Return a 32 bit version in form MMMMnnpp - MMMM = major verison, nn = minor version, pp = patch version */ 
OCSD_C_API uint32_t ocsd_get_version(void) 
{ 
    return ocsdVersion::vers_num();
}

/** Get library version string */
OCSD_C_API const char * ocsd_get_version_str(void) 
{ 
    return ocsdVersion::vers_str();
}


/*** Decode tree creation etc. */

OCSD_C_API dcd_tree_handle_t ocsd_create_dcd_tree(const ocsd_dcd_tree_src_t src_type, const uint32_t deformatterCfgFlags)
{
    dcd_tree_handle_t handle = C_API_INVALID_TREE_HANDLE;
    handle = (dcd_tree_handle_t)DecodeTree::CreateDecodeTree(src_type,deformatterCfgFlags); 
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        lib_dt_data_list *pList = new (std::nothrow) lib_dt_data_list;
        if(pList != 0)
        {
            s_data_map.insert(std::pair<dcd_tree_handle_t, lib_dt_data_list *>(handle,pList));
        }
        else
        {
            ocsd_destroy_dcd_tree(handle);
            handle = C_API_INVALID_TREE_HANDLE;
        }
    }
    return handle;
}

OCSD_C_API void ocsd_destroy_dcd_tree(const dcd_tree_handle_t handle)
{
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        GenTraceElemCBObj * pIf = (GenTraceElemCBObj *)(((DecodeTree *)handle)->getGenTraceElemOutI());
        if(pIf != 0)
            delete pIf;

        /* need to clear any associated callback data. */
        std::map<dcd_tree_handle_t, lib_dt_data_list *>::iterator it;
        it = s_data_map.find(handle);
        if(it != s_data_map.end())
        {
            std::vector<ITrcTypedBase *>::iterator itcb;
            itcb = it->second->cb_objs.begin();
            while(itcb != it->second->cb_objs.end())
            {
                delete *itcb;
                itcb++;
            }
            it->second->cb_objs.clear();
            delete it->second;
            s_data_map.erase(it);
        }
        DecodeTree::DestroyDecodeTree((DecodeTree *)handle);
    }
}

/*** Decode tree process data */

OCSD_C_API ocsd_datapath_resp_t ocsd_dt_process_data(const dcd_tree_handle_t handle,
                                            const ocsd_datapath_op_t op,
                                            const ocsd_trc_index_t index,
                                            const uint32_t dataBlockSize,
                                            const uint8_t *pDataBlock,
                                            uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp =  OCSD_RESP_FATAL_NOT_INIT;
    if(handle != C_API_INVALID_TREE_HANDLE)
        resp = ((DecodeTree *)handle)->TraceDataIn(op,index,dataBlockSize,pDataBlock,numBytesProcessed);
    return resp;
}

/*** Decode tree - decoder management */

OCSD_C_API ocsd_err_t ocsd_dt_create_decoder(const dcd_tree_handle_t handle,
                                             const char *decoder_name,
                                             const int create_flags,
                                             const void *decoder_cfg,
                                             unsigned char *pCSID
                                             )
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *dt = (DecodeTree *)handle;
    std::string dName = decoder_name;
    IDecoderMngr *pDcdMngr;
    err = OcsdLibDcdRegister::getDecoderRegister()->getDecoderMngrByName(dName,&pDcdMngr);
    if(err != OCSD_OK)
        return err;

    CSConfig *pConfig = 0;
    err = pDcdMngr->createConfigFromDataStruct(&pConfig,decoder_cfg);
    if(err != OCSD_OK)
        return err;

    err = dt->createDecoder(dName,create_flags,pConfig);
    if(err == OCSD_OK)
        *pCSID = pConfig->getTraceID();
    delete pConfig;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_remove_decoder(   const dcd_tree_handle_t handle, 
                                                const unsigned char CSID)
{
    return ((DecodeTree *)handle)->removeDecoder(CSID);
}

OCSD_C_API ocsd_err_t ocsd_dt_attach_packet_callback(  const dcd_tree_handle_t handle, 
                                                const unsigned char CSID,
                                                const ocsd_c_api_cb_types callback_type, 
                                                void *p_fn_callback_data,
                                                const void *p_context)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT = static_cast<DecodeTree *>(handle);
    DecodeTreeElement *pElem = pDT->getDecoderElement(CSID);
    if(pElem == 0)
        return OCSD_ERR_INVALID_ID;  // cannot find entry for that CSID

    ITrcTypedBase *pDataInSink = 0;  // pointer to a sink callback object
    switch(callback_type)
    {
    case OCSD_C_API_CB_PKT_SINK:
        err = ocsd_create_pkt_sink_cb(pElem->getProtocol(),(FnDefPktDataIn)p_fn_callback_data,p_context,&pDataInSink);
        if(err == OCSD_OK)
            err = pElem->getDecoderMngr()->attachPktSink(pElem->getDecoderHandle(), pDataInSink);
        break;

    case OCSD_C_API_CB_PKT_MON:
        err = ocsd_create_pkt_mon_cb(pElem->getProtocol(),(FnDefPktDataMon)p_fn_callback_data,p_context,&pDataInSink);
        if (err == OCSD_OK)
            err = pElem->getDecoderMngr()->attachPktMonitor(pElem->getDecoderHandle(), pDataInSink);
        break;

    default:
        err = OCSD_ERR_INVALID_PARAM_VAL;
    }

    if(err == OCSD_OK)
    {
        if (err == OCSD_OK)
        {
            // save object pointer for destruction later.
            std::map<dcd_tree_handle_t, lib_dt_data_list *>::iterator it;
            it = s_data_map.find(handle);
            if (it != s_data_map.end())
                it->second->cb_objs.push_back(pDataInSink);
        }
        else
            delete pDataInSink;
    }
    return err;
}

/*** Decode tree set element output */

OCSD_C_API ocsd_err_t ocsd_dt_set_gen_elem_outfn(const dcd_tree_handle_t handle, FnTraceElemIn pFn, const void *p_context)
{

    GenTraceElemCBObj * pCBObj = new (std::nothrow)GenTraceElemCBObj(pFn, p_context);
    if(pCBObj)
    {
        ((DecodeTree *)handle)->setGenTraceElemOutI(pCBObj);
        return OCSD_OK;
    }
    return OCSD_ERR_MEM;
}


/*** Default error logging */

OCSD_C_API ocsd_err_t ocsd_def_errlog_init(const ocsd_err_severity_t verbosity, const int create_output_logger)
{
    if(DecodeTree::getDefaultErrorLogger()->initErrorLogger(verbosity,(bool)(create_output_logger != 0)))
        return OCSD_OK;
    return OCSD_ERR_NOT_INIT;
}

OCSD_C_API ocsd_err_t ocsd_def_errlog_config_output(const int output_flags, const char *log_file_name)
{
    ocsdMsgLogger *pLogger = DecodeTree::getDefaultErrorLogger()->getOutputLogger();
    if(pLogger)
    {
        pLogger->setLogOpts(output_flags & C_API_MSGLOGOUT_MASK);
        if(log_file_name != NULL)
        {
            pLogger->setLogFileName(log_file_name);
        }
        return OCSD_OK;
    }
    return OCSD_ERR_NOT_INIT;    
}


OCSD_C_API ocsd_err_t ocsd_def_errlog_set_strprint_cb(const dcd_tree_handle_t handle, void *p_context, FnDefLoggerPrintStrCB p_str_print_cb)
{
    ocsdMsgLogger *pLogger = DecodeTree::getDefaultErrorLogger()->getOutputLogger();
    if (pLogger)
    {
        std::map<dcd_tree_handle_t, lib_dt_data_list *>::iterator it;
        it = s_data_map.find(handle);
        if (it != s_data_map.end())
        {
            DefLogStrCBObj *pCBObj = &(it->second->s_def_log_str_cb);
            pCBObj->setCBFn(p_context, p_str_print_cb);
            pLogger->setStrOutFn(pCBObj);
            int logOpts = pLogger->getLogOpts();
            logOpts |= (int)(ocsdMsgLogger::OUT_STR_CB);
            pLogger->setLogOpts(logOpts);
            return OCSD_OK;
        }
    }
    return OCSD_ERR_NOT_INIT;
}

OCSD_C_API void ocsd_def_errlog_msgout(const char *msg)
{
    ocsdMsgLogger *pLogger = DecodeTree::getDefaultErrorLogger()->getOutputLogger();
    if(pLogger)
        pLogger->LogMsg(msg);
}

/*** Convert packet to string */

OCSD_C_API ocsd_err_t ocsd_pkt_str(const ocsd_trace_protocol_t pkt_protocol, const void *p_pkt, char *buffer, const int buffer_size)
{
    ocsd_err_t err = OCSD_OK;
    if((buffer == NULL) || (buffer_size < 2))
        return OCSD_ERR_INVALID_PARAM_VAL;

    std::string pktStr = "";
    buffer[0] = 0;

    switch(pkt_protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        trcPrintElemToString<EtmV4ITrcPacket,ocsd_etmv4_i_pkt>(p_pkt, pktStr);
        break;

    case OCSD_PROTOCOL_ETMV3:
        trcPrintElemToString<EtmV3TrcPacket,ocsd_etmv3_pkt>(p_pkt, pktStr);
        break;

    case OCSD_PROTOCOL_STM:
        trcPrintElemToString<StmTrcPacket,ocsd_stm_pkt>(p_pkt, pktStr);
        break;

    case OCSD_PROTOCOL_PTM:
        trcPrintElemToString<PtmTrcPacket,ocsd_ptm_pkt>(p_pkt, pktStr);
        break;

    default:
        if (OCSD_PROTOCOL_IS_CUSTOM(pkt_protocol))
            err = ocsd_cust_protocol_to_str(pkt_protocol, p_pkt, buffer, buffer_size);
        else
            err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if(pktStr.size() > 0)
    {
        strncpy(buffer,pktStr.c_str(),buffer_size-1);
        buffer[buffer_size-1] = 0;
    }
    return err;
}

OCSD_C_API ocsd_err_t ocsd_gen_elem_str(const ocsd_generic_trace_elem *p_pkt, char *buffer, const int buffer_size)
{
    ocsd_err_t err = OCSD_OK;
    if((buffer == NULL) || (buffer_size < 2))
        return OCSD_ERR_INVALID_PARAM_VAL;
    std::string str;
    trcPrintElemToString<OcsdTraceElement,ocsd_generic_trace_elem>(p_pkt,str);
    if(str.size() > 0)
    {
        strncpy(buffer,str.c_str(),buffer_size -1);
        buffer[buffer_size-1] = 0;
    }
    return err;
}

/*** Decode tree -- memory accessor control */

OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const char *filepath)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT;
    err = ocsd_check_and_add_mem_acc_mapper(handle,&pDT);
    if(err == OCSD_OK)
        err = pDT->addBinFileMemAcc(address,mem_space,filepath);
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_region_mem_acc(const dcd_tree_handle_t handle, const ocsd_file_mem_region_t *region_array, const int num_regions, const ocsd_mem_space_acc_t mem_space, const char *filepath)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT;
    err = ocsd_check_and_add_mem_acc_mapper(handle,&pDT);
    if(err == OCSD_OK)
        err = pDT->addBinFileRegionMemAcc(region_array,num_regions,mem_space,filepath);
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_buffer_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t *p_mem_buffer, const uint32_t mem_length)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT;
    err = ocsd_check_and_add_mem_acc_mapper(handle,&pDT);
    if(err == OCSD_OK)
        err = pDT->addBufferMemAcc(address,mem_space,p_mem_buffer,mem_length);
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_callback_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_vaddr_t en_address, const ocsd_mem_space_acc_t mem_space, Fn_MemAcc_CB p_cb_func, const void *p_context)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT;
    err = ocsd_check_and_add_mem_acc_mapper(handle,&pDT);
    if(err == OCSD_OK)
        err = pDT->addCallbackMemAcc(st_address,en_address,mem_space,p_cb_func,p_context);
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_remove_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_mem_space_acc_t mem_space)
{
    ocsd_err_t err = OCSD_OK;

    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        err = pDT->removeMemAccByAddress(st_address,mem_space);
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API void ocsd_tl_log_mapped_mem_ranges(const dcd_tree_handle_t handle)
{
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        pDT->logMappedRanges();
    }
}

OCSD_C_API void ocsd_gen_elem_init(ocsd_generic_trace_elem *p_pkt, const ocsd_gen_trc_elem_t elem_type)
{
    p_pkt->elem_type = elem_type;
    p_pkt->flag_bits = 0;
    p_pkt->ptr_extended_data = 0;
}

OCSD_C_API ocsd_err_t ocsd_dt_set_raw_frame_printer(const dcd_tree_handle_t handle, int flags)
{
    if (handle != C_API_INVALID_TREE_HANDLE)
        return ((DecodeTree *)handle)->addRawFramePrinter(0, (uint32_t)flags);
    return OCSD_ERR_NOT_INIT;
}

OCSD_C_API ocsd_err_t ocsd_dt_set_gen_elem_printer(const dcd_tree_handle_t handle)
{
    if (handle != C_API_INVALID_TREE_HANDLE)
        return ((DecodeTree *)handle)->addGenElemPrinter(0);
    return OCSD_ERR_NOT_INIT;
}

OCSD_C_API ocsd_err_t ocsd_dt_set_pkt_protocol_printer(const dcd_tree_handle_t handle, uint8_t cs_id, int monitor)
{
    ocsd_err_t err = OCSD_ERR_NOT_INIT;
    if (handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *p_tree = (DecodeTree *)handle;
        err = p_tree->addPacketPrinter(cs_id, (bool)(monitor != 0), 0);
    }
    return err;
}

/*******************************************************************************/
/* C API local fns                                                             */
/*******************************************************************************/
static ocsd_err_t ocsd_create_pkt_sink_cb(ocsd_trace_protocol_t protocol,  FnDefPktDataIn pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj )
{
    ocsd_err_t err = OCSD_OK;
    *ppCBObj = 0;

    switch(protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        *ppCBObj = new (std::nothrow) PktCBObj<EtmV4ITrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_ETMV3:
        *ppCBObj = new (std::nothrow) PktCBObj<EtmV3TrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_PTM:
        *ppCBObj = new (std::nothrow) PktCBObj<PtmTrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_STM:
        *ppCBObj = new (std::nothrow) PktCBObj<StmTrcPacket>(pPktInFn,p_context); 
        break;

    default:
        if ((protocol >= OCSD_PROTOCOL_CUSTOM_0) && (protocol < OCSD_PROTOCOL_END))
        {
            *ppCBObj = new (std::nothrow) PktCBObj<void>(pPktInFn, p_context);                
        }
        else
            err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if((*ppCBObj == 0) && (err == OCSD_OK))
        err = OCSD_ERR_MEM;

    return err;
}

static ocsd_err_t ocsd_create_pkt_mon_cb(ocsd_trace_protocol_t protocol, FnDefPktDataMon pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj )
{
    ocsd_err_t err = OCSD_OK;
    *ppCBObj = 0;

    switch(protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        *ppCBObj = new (std::nothrow) PktMonCBObj<EtmV4ITrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_ETMV3:
        *ppCBObj = new (std::nothrow) PktMonCBObj<EtmV3TrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_PTM:
        *ppCBObj = new (std::nothrow) PktMonCBObj<PtmTrcPacket>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_STM:
        *ppCBObj = new (std::nothrow) PktMonCBObj<StmTrcPacket>(pPktInFn,p_context); 
        break;

    default:
        if ((protocol >= OCSD_PROTOCOL_CUSTOM_0) && (protocol < OCSD_PROTOCOL_END))
        {
            *ppCBObj = new (std::nothrow) PktMonCBObj<void>(pPktInFn, p_context);
        }
        else
            err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if((*ppCBObj == 0) && (err == OCSD_OK))
        err = OCSD_ERR_MEM;

    return err;
}

static ocsd_err_t ocsd_check_and_add_mem_acc_mapper(const dcd_tree_handle_t handle, DecodeTree **ppDT)
{
    *ppDT = 0;
    if(handle == C_API_INVALID_TREE_HANDLE)
        return OCSD_ERR_INVALID_PARAM_VAL;
    *ppDT = static_cast<DecodeTree *>(handle);
    if(!(*ppDT)->hasMemAccMapper())
        return (*ppDT)->createMemAccMapper();
    return OCSD_OK;
}

/*******************************************************************************/
/* C API Helper objects                                                        */
/*******************************************************************************/

/****************** Generic trace element output callback function  ************/
GenTraceElemCBObj::GenTraceElemCBObj(FnTraceElemIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t GenTraceElemCBObj::TraceElemIn(const ocsd_trc_index_t index_sop,
                                              const uint8_t trc_chan_id,
                                              const OcsdTraceElement &elem)
{
    return m_c_api_cb_fn(m_p_cb_context, index_sop, trc_chan_id, &elem);
}

/* End of File ocsd_c_api.cpp */
