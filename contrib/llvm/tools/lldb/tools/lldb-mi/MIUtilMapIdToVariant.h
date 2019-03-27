//===-- MIUtilMapIdToVariant.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include <map>

// In-house headers:
#include "MICmnBase.h"
#include "MICmnResources.h"
#include "MIUtilString.h"
#include "MIUtilVariant.h"

//++
//============================================================================
// Details: MI common code utility class. Map type container that hold general
//          object types (by being a variant wrapper)
//          objects by ID.
//--
class CMIUtilMapIdToVariant : public CMICmnBase {
  // Methods:
public:
  /* ctor */ CMIUtilMapIdToVariant();

  template <typename T> bool Add(const CMIUtilString &vId, const T &vData);
  void Clear();
  template <typename T>
  bool Get(const CMIUtilString &vId, T &vrwData, bool &vrwbFound) const;
  bool HaveAlready(const CMIUtilString &vId) const;
  bool IsEmpty() const;
  bool Remove(const CMIUtilString &vId);

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMIUtilMapIdToVariant() override;

  // Typedefs:
private:
  typedef std::map<CMIUtilString, CMIUtilVariant> MapKeyToVariantValue_t;
  typedef std::pair<CMIUtilString, CMIUtilVariant> MapPairKeyToVariantValue_t;

  // Methods:
private:
  bool IsValid(const CMIUtilString &vId) const;

  // Attributes:
  MapKeyToVariantValue_t m_mapKeyToVariantValue;
};

//++
//------------------------------------------------------------------------------------
// Details: Add to *this container a data object of general type identified by
// an ID.
//          If the data with that ID already exists in the container it is
//          replace with
//          the new data specified.
// Type:    Method.
// Args:    T       - The data object's variable type.
//          vId     - (R) Unique ID i.e. GUID.
//          vData   - (R) The general data object to be stored of some type.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
template <typename T>
bool CMIUtilMapIdToVariant::Add(const CMIUtilString &vId, const T &vData) {
  if (!IsValid(vId)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_VARIANT_ERR_MAP_KEY_INVALID), vId.c_str()));
    return MIstatus::failure;
  }

  const bool bOk = HaveAlready(vId) ? Remove(vId) : MIstatus::success;
  if (bOk) {
    CMIUtilVariant data;
    data.Set<T>(vData);
    MapPairKeyToVariantValue_t pr(vId, data);
    m_mapKeyToVariantValue.insert(pr);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve a data object from *this container identified by the
// specified ID.
// Type:    Method.
// Args:    T           - The data object's variable type.
//          vId         - (R) Unique ID i.e. GUID.
//          vrwData     - (W) Copy of the data object held.
//          vrwbFound   - (W) True = data found, false = data not found.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
template <typename T>
bool CMIUtilMapIdToVariant::Get(const CMIUtilString &vId, T &vrwData,
                                bool &vrwbFound) const {
  vrwbFound = false;

  if (!IsValid(vId)) {
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_VARIANT_ERR_MAP_KEY_INVALID), vId.c_str()));
    return MIstatus::failure;
  }

  const MapKeyToVariantValue_t::const_iterator it =
      m_mapKeyToVariantValue.find(vId);
  if (it != m_mapKeyToVariantValue.end()) {
    const CMIUtilVariant &rData = (*it).second;
    const T *pDataObj = rData.Get<T>();
    if (pDataObj != nullptr) {
      vrwbFound = true;
      vrwData = *pDataObj;
      return MIstatus::success;
    } else {
      SetErrorDescription(MIRSRC(IDS_VARIANT_ERR_USED_BASECLASS));
      return MIstatus::failure;
    }
  }

  return MIstatus::success;
}
