//===-- MIUtilVariant.cpp----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MIUtilVariant.h"

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase::CDataObjectBase() {}

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase::CDataObjectBase(
    const CDataObjectBase &vrOther) {
  MIunused(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase::CDataObjectBase(CDataObjectBase &vrOther) {
  MIunused(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase move constructor.
// Type:    Method.
// Args:    vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase::CDataObjectBase(CDataObjectBase &&vrwOther) {
  MIunused(vrwOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase::~CDataObjectBase() { Destroy(); }

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase copy assignment.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase &CMIUtilVariant::CDataObjectBase::
operator=(const CDataObjectBase &vrOther) {
  Copy(vrOther);
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObjectBase move assignment.
// Type:    Method.
// Args:    vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase &CMIUtilVariant::CDataObjectBase::
operator=(CDataObjectBase &&vrwOther) {
  Copy(vrwOther);
  vrwOther.Destroy();
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: Create a new copy of *this class.
// Type:    Overrideable.
// Args:    None.
// Return:  CDataObjectBase *   - Pointer to a new object.
// Throws:  None.
//--
CMIUtilVariant::CDataObjectBase *
CMIUtilVariant::CDataObjectBase::CreateCopyOfSelf() {
  // Override to implement copying of variant's data object
  return new CDataObjectBase();
}

//++
//------------------------------------------------------------------------------------
// Details: Determine if *this object is a derived from CDataObjectBase.
// Type:    Overrideable.
// Args:    None.
// Return:  bool    - True = *this is derived from CDataObjectBase, false =
// *this is instance of the this base class.
// Throws:  None.
//--
bool CMIUtilVariant::CDataObjectBase::GetIsDerivedClass() const {
  // Override to in the derived class and return true
  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: Perform a bitwise copy of *this object.
// Type:    Overrideable.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
void CMIUtilVariant::CDataObjectBase::Copy(const CDataObjectBase &vrOther) {
  // Override to implement
  MIunused(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: Release any resources used by *this object.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilVariant::CDataObjectBase::Destroy() {
  // Do nothing - override to implement
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CDataObject copy constructor.
// Type:    Method.
// Args:    T       - The object's type.
//          vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T>::CDataObject(const CDataObject &vrOther) {
  if (this == &vrOther)
    return;
  Copy(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject copy constructor.
// Type:    Method.
// Args:    T       - The object's type.
//          vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T>::CDataObject(CDataObject &vrOther) {
  if (this == &vrOther)
    return;
  Copy(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject move constructor.
// Type:    Method.
// Args:    T           - The object's type.
//          vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T>::CDataObject(CDataObject &&vrwOther) {
  if (this == &vrwOther)
    return;
  Copy(vrwOther);
  vrwOther.Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject copy assignment.
// Type:    Method.
// Args:    T       - The object's type.
//          vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T> &CMIUtilVariant::CDataObject<T>::
operator=(const CDataObject &vrOther) {
  if (this == &vrOther)
    return *this;
  Copy(vrOther);
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject move assignment.
// Type:    Method.
// Args:    T           - The object's type.
//          vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T> &CMIUtilVariant::CDataObject<T>::
operator=(CDataObject &&vrwOther) {
  if (this == &vrwOther)
    return *this;
  Copy(vrwOther);
  vrwOther.Destroy();
  return *this;
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CMIUtilVariant() : m_pDataObject(nullptr) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CMIUtilVariant(const CMIUtilVariant &vrOther)
    : m_pDataObject(nullptr) {
  if (this == &vrOther)
    return;

  Copy(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant copy constructor.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CMIUtilVariant(CMIUtilVariant &vrOther)
    : m_pDataObject(nullptr) {
  if (this == &vrOther)
    return;

  Copy(vrOther);
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant move constructor.
// Type:    Method.
// Args:    vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::CMIUtilVariant(CMIUtilVariant &&vrwOther)
    : m_pDataObject(nullptr) {
  if (this == &vrwOther)
    return;

  Copy(vrwOther);
  vrwOther.Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant::~CMIUtilVariant() { Destroy(); }

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant copy assignment.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant &CMIUtilVariant::operator=(const CMIUtilVariant &vrOther) {
  if (this == &vrOther)
    return *this;

  Copy(vrOther);
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilVariant move assignment.
// Type:    Method.
// Args:    vrwOther    - (R) The other object.
// Return:  None.
// Throws:  None.
//--
CMIUtilVariant &CMIUtilVariant::operator=(CMIUtilVariant &&vrwOther) {
  if (this == &vrwOther)
    return *this;

  Copy(vrwOther);
  vrwOther.Destroy();
  return *this;
}

//++
//------------------------------------------------------------------------------------
// Details: Release the resources used by *this object.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilVariant::Destroy() {
  if (m_pDataObject != nullptr)
    delete m_pDataObject;
  m_pDataObject = nullptr;
}

//++
//------------------------------------------------------------------------------------
// Details: Bitwise copy another data object to *this variant object.
// Type:    Method.
// Args:    vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
void CMIUtilVariant::Copy(const CMIUtilVariant &vrOther) {
  Destroy();

  if (vrOther.m_pDataObject != nullptr) {
    m_pDataObject = vrOther.m_pDataObject->CreateCopyOfSelf();
  }
}
