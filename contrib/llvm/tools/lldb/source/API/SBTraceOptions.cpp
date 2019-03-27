//===-- SBTraceOptions.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTraceOptions.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/TraceOptions.h"

using namespace lldb;
using namespace lldb_private;

SBTraceOptions::SBTraceOptions() {
  m_traceoptions_sp.reset(new TraceOptions());
}

lldb::TraceType SBTraceOptions::getType() const {
  if (m_traceoptions_sp)
    return m_traceoptions_sp->getType();
  return lldb::TraceType::eTraceTypeNone;
}

uint64_t SBTraceOptions::getTraceBufferSize() const {
  if (m_traceoptions_sp)
    return m_traceoptions_sp->getTraceBufferSize();
  return 0;
}

lldb::SBStructuredData SBTraceOptions::getTraceParams(lldb::SBError &error) {
  error.Clear();
  const lldb_private::StructuredData::DictionarySP dict_obj =
      m_traceoptions_sp->getTraceParams();
  lldb::SBStructuredData structData;
  if (dict_obj && structData.m_impl_up)
    structData.m_impl_up->SetObjectSP(dict_obj->shared_from_this());
  else
    error.SetErrorString("Empty trace params");
  return structData;
}

uint64_t SBTraceOptions::getMetaDataBufferSize() const {
  if (m_traceoptions_sp)
    return m_traceoptions_sp->getTraceBufferSize();
  return 0;
}

void SBTraceOptions::setTraceParams(lldb::SBStructuredData &params) {
  if (m_traceoptions_sp && params.m_impl_up) {
    StructuredData::ObjectSP obj_sp = params.m_impl_up->GetObjectSP();
    if (obj_sp && obj_sp->GetAsDictionary() != nullptr)
      m_traceoptions_sp->setTraceParams(
          std::static_pointer_cast<StructuredData::Dictionary>(obj_sp));
  }
  return;
}

void SBTraceOptions::setType(lldb::TraceType type) {
  if (m_traceoptions_sp)
    m_traceoptions_sp->setType(type);
}

void SBTraceOptions::setTraceBufferSize(uint64_t size) {
  if (m_traceoptions_sp)
    m_traceoptions_sp->setTraceBufferSize(size);
}

void SBTraceOptions::setMetaDataBufferSize(uint64_t size) {
  if (m_traceoptions_sp)
    m_traceoptions_sp->setMetaDataBufferSize(size);
}

bool SBTraceOptions::IsValid() {
  if (m_traceoptions_sp)
    return true;
  return false;
}

void SBTraceOptions::setThreadID(lldb::tid_t thread_id) {
  if (m_traceoptions_sp)
    m_traceoptions_sp->setThreadID(thread_id);
}

lldb::tid_t SBTraceOptions::getThreadID() {
  if (m_traceoptions_sp)
    return m_traceoptions_sp->getThreadID();
  return LLDB_INVALID_THREAD_ID;
}
