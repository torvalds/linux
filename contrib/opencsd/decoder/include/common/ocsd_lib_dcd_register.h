/*
 * \file       ocsd_lib_dcd_register.h
 * \brief      OpenCSD : Library decoder registration and management.
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
 */

#ifndef ARM_OCSD_LIB_DCD_REGISTER_H_INCLUDED
#define ARM_OCSD_LIB_DCD_REGISTER_H_INCLUDED


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

#include <map>

#include "opencsd/ocsd_if_types.h"
#include "common/ocsd_dcd_mngr_i.h"

/*!
 * @class OcsdLibDcdRegister : Registers decoders with the library 
 *
 * library decoder register class allows decoders to be registered by name, and the register allows clients to access 
 * the list of names of registerd decoders.
 * 
 * The decoders in the library are accessed through the decoder manager interface. This provides a set of functions to allow 
 * the creation, manipulation and destruction of registered decoders
 *
 */
class OcsdLibDcdRegister
{
public:
    static OcsdLibDcdRegister *getDecoderRegister();

    static void deregisterAllDecoders();    //!< library cleanup - deregisters decoder managers and destroys the register object.
    static const ocsd_trace_protocol_t getNextCustomProtocolID();
    static void releaseLastCustomProtocolID();

    const ocsd_err_t registerDecoderTypeByName(const std::string &name, IDecoderMngr *p_decoder_fact);  //!< register a decoder manager interface   
    const ocsd_err_t getDecoderMngrByName(const std::string &name, IDecoderMngr **p_decoder_mngr);
    const ocsd_err_t getDecoderMngrByType(const ocsd_trace_protocol_t decoderType, IDecoderMngr **p_decoder_mngr);

    const bool isRegisteredDecoder(const std::string &name);
    const bool getFirstNamedDecoder(std::string &name); 
    const bool getNextNamedDecoder(std::string &name);

    const bool isRegisteredDecoderType(const ocsd_trace_protocol_t decoderType);

private:
    void registerBuiltInDecoders();         //!< register the list of build in decoder managers on first access of getDecoderMngrByName.
    void deRegisterCustomDecoders();        //!< delete all custom decoders registered with the library.

    std::map<const std::string, IDecoderMngr *> m_decoder_mngrs;                    //!< map linking names to decoder manager interfaces.
    std::map<const std::string, IDecoderMngr *>::const_iterator m_iter;             //!< iterator for name search.

    std::map<const ocsd_trace_protocol_t, IDecoderMngr *> m_typed_decoder_mngrs;    //!< map linking decoder managers to protocol type ID

    // cache last found by type to speed up repeated quries on same object.
    IDecoderMngr *m_pLastTypedDecoderMngr;      //!< last manager we found by type



    // singleton pattern - need just one of these in the library - ensure all default constructors are private.
    OcsdLibDcdRegister();
    OcsdLibDcdRegister(OcsdLibDcdRegister const &) {};
    OcsdLibDcdRegister& operator=(OcsdLibDcdRegister const &){ return *this; };
    ~OcsdLibDcdRegister();

    static OcsdLibDcdRegister *m_p_libMngr;
    static bool m_b_registeredBuiltins;
    static ocsd_trace_protocol_t m_nextCustomProtocolID;  
};

/*!
 * Typedef of function signature to create a decoder manager.
 *
 * @param *name : Registered name of the decoder.
 */
typedef IDecoderMngr *(*CreateMngr)(const std::string &name);

/*!
 * Template function to create a specific decoder manager class object.
 *
 * @param &name : Registered name of the decoder.
 *
 * @return IDecoderMngr *  : pointer to the decoder manager base class interface.
 */
template <typename T> IDecoderMngr *createManagerInst(const std::string &name)
{
    return new (std::nothrow)T(name);
}

/*! Structure to contain the information needed to create and register a builtin decoder 
 *  manager with the library
 */
typedef struct built_in_decoder_info {
    IDecoderMngr *pMngr;    //!< pointer to created decoder manager
    CreateMngr PFn;         //!< function to create the decoder manager.
    const char *name;       //!< registered name of the decoder.
} built_in_decoder_info_t;

//! Define to use to fill in an array of built_in_decoder_info_t structures.
#define CREATE_BUILTIN_ENTRY(C,N) { 0, createManagerInst<C>, N }

#endif // ARM_OCSD_LIB_DCD_REGISTER_H_INCLUDED

/* End of File ocsd_lib_dcd_register.h */
