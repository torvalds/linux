//===-- MIUtilVariant.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MIDataTypes.h"

//++
//============================================================================
// Details: MI common code utility class. The class implements behaviour of a
//          variant object which holds any data object of type T. A copy of the
//          data object specified is made and stored in *this wrapper. When the
//          *this object is destroyed the data object hold within calls its
//          destructor should it have one.
//--
class CMIUtilVariant {
  // Methods:
public:
  /* ctor */ CMIUtilVariant();
  /* ctor */ CMIUtilVariant(const CMIUtilVariant &vrOther);
  /* ctor */ CMIUtilVariant(CMIUtilVariant &vrOther);
  /* ctor */ CMIUtilVariant(CMIUtilVariant &&vrwOther);
  /* dtor */ ~CMIUtilVariant();

  template <typename T> void Set(const T &vArg);
  template <typename T> T *Get() const;

  CMIUtilVariant &operator=(const CMIUtilVariant &vrOther);
  CMIUtilVariant &operator=(CMIUtilVariant &&vrwOther);

  // Classes:
private:
  //++ ----------------------------------------------------------------------
  // Details: Base class wrapper to hold the variant's data object when
  //          assigned to it by the Set() function. Do not use the
  //          CDataObjectBase
  //          to create objects, use only CDataObjectBase derived objects,
  //          see CDataObject() class.
  //--
  class CDataObjectBase {
    // Methods:
  public:
    /* ctor */ CDataObjectBase();
    /* ctor */ CDataObjectBase(const CDataObjectBase &vrOther);
    /* ctor */ CDataObjectBase(CDataObjectBase &vrOther);
    /* ctor */ CDataObjectBase(CDataObjectBase &&vrwOther);
    //
    CDataObjectBase &operator=(const CDataObjectBase &vrOther);
    CDataObjectBase &operator=(CDataObjectBase &&vrwOther);

    // Overrideable:
  public:
    virtual ~CDataObjectBase();
    virtual CDataObjectBase *CreateCopyOfSelf();
    virtual bool GetIsDerivedClass() const;

    // Overrideable:
  protected:
    virtual void Copy(const CDataObjectBase &vrOther);
    virtual void Destroy();
  };

  //++ ----------------------------------------------------------------------
  // Details: Derived from CDataObjectBase, this class is the wrapper for the
  //          data object as it has an aggregate of type T which is a copy
  //          of the data object assigned to the variant object.
  //--
  template <typename T> class CDataObject : public CDataObjectBase {
    // Methods:
  public:
    /* ctor */ CDataObject();
    /* ctor */ CDataObject(const T &vArg);
    /* ctor */ CDataObject(const CDataObject &vrOther);
    /* ctor */ CDataObject(CDataObject &vrOther);
    /* ctor */ CDataObject(CDataObject &&vrwOther);
    //
    CDataObject &operator=(const CDataObject &vrOther);
    CDataObject &operator=(CDataObject &&vrwOther);
    //
    T &GetDataObject();

    // Overridden:
  public:
    // From CDataObjectBase
    ~CDataObject() override;
    CDataObjectBase *CreateCopyOfSelf() override;
    bool GetIsDerivedClass() const override;

    // Overrideable:
  private:
    virtual void Duplicate(const CDataObject &vrOther);

    // Overridden:
  private:
    // From CDataObjectBase
    void Destroy() override;

    // Attributes:
  private:
    T m_dataObj;
  };

  // Methods
private:
  void Destroy();
  void Copy(const CMIUtilVariant &vrOther);

  // Attributes:
private:
  CDataObjectBase *m_pDataObject;
};

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CDataObject constructor.
// Type:    Method.
// Args:    T   - The object's type.
// Return:  None.
// Throws:  None.
//--
template <typename T> CMIUtilVariant::CDataObject<T>::CDataObject() {}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject constructor.
// Type:    Method.
// Args:    T       - The object's type.
//          vArg    - (R) The data object to be stored in the variant object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObject<T>::CDataObject(const T &vArg) {
  m_dataObj = vArg;
}

//++
//------------------------------------------------------------------------------------
// Details: CDataObject destructor.
// Type:    Overridden.
// Args:    T   - The object's type.
// Return:  None.
// Throws:  None.
//--
template <typename T> CMIUtilVariant::CDataObject<T>::~CDataObject() {
  Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the data object hold by *this object wrapper.
// Type:    Method.
// Args:    T   - The object's type.
// Return:  T & - Reference to the data object.
// Throws:  None.
//--
template <typename T> T &CMIUtilVariant::CDataObject<T>::GetDataObject() {
  return m_dataObj;
}

//++
//------------------------------------------------------------------------------------
// Details: Create a new copy of *this class.
// Type:    Overridden.
// Args:    T   - The object's type.
// Return:  CDataObjectBase *   - Pointer to a new object.
// Throws:  None.
//--
template <typename T>
CMIUtilVariant::CDataObjectBase *
CMIUtilVariant::CDataObject<T>::CreateCopyOfSelf() {
  CDataObject *pCopy = new CDataObject<T>(m_dataObj);

  return pCopy;
}

//++
//------------------------------------------------------------------------------------
// Details: Determine if *this object is a derived from CDataObjectBase.
// Type:    Overridden.
// Args:    T   - The object's type.
// Return:  bool    - True = *this is derived from CDataObjectBase
//                  - False = *this is an instance of the base class.
// Throws:  None.
//--
template <typename T>
bool CMIUtilVariant::CDataObject<T>::GetIsDerivedClass() const {
  return true;
}

//++
//------------------------------------------------------------------------------------
// Details: Perform a bitwise copy of *this object.
// Type:    Overrideable.
// Args:    T       - The object's type.
//          vrOther - (R) The other object.
// Return:  None.
// Throws:  None.
//--
template <typename T>
void CMIUtilVariant::CDataObject<T>::Duplicate(const CDataObject &vrOther) {
  CDataObjectBase::Copy(vrOther);
  m_dataObj = vrOther.m_dataObj;
}

//++
//------------------------------------------------------------------------------------
// Details: Release any resources used by *this object.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
template <typename T> void CMIUtilVariant::CDataObject<T>::Destroy() {
  CDataObjectBase::Destroy();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: Assign to the variant an object of a specified type.
// Type:    Template method.
// Args:    T       - The object's type.
//          vArg    - (R) The object to store.
// Return:  None.
// Throws:  None.
//--
template <typename T> void CMIUtilVariant::Set(const T &vArg) {
  m_pDataObject = new CDataObject<T>(vArg);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the data object from *this variant.
// Type:    Template method.
// Args:    T   - The object's type.
// Return:  T * - Pointer the data object, NULL = data object not assigned to
// *this variant.
// Throws:  None.
//--
template <typename T> T *CMIUtilVariant::Get() const {
  if ((m_pDataObject != nullptr) && m_pDataObject->GetIsDerivedClass()) {
    CDataObject<T> *pDataObj = static_cast<CDataObject<T> *>(m_pDataObject);
    return &pDataObj->GetDataObject();
  }

  // Do not use a CDataObjectBase object, use only CDataObjectBase derived
  // objects
  return nullptr;
}
