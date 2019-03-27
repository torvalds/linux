//===-- ModuleChild.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ModuleChild.h"

using namespace lldb_private;

ModuleChild::ModuleChild(const lldb::ModuleSP &module_sp)
    : m_module_wp(module_sp) {}

ModuleChild::ModuleChild(const ModuleChild &rhs)
    : m_module_wp(rhs.m_module_wp) {}

ModuleChild::~ModuleChild() {}

const ModuleChild &ModuleChild::operator=(const ModuleChild &rhs) {
  if (this != &rhs)
    m_module_wp = rhs.m_module_wp;
  return *this;
}

lldb::ModuleSP ModuleChild::GetModule() const { return m_module_wp.lock(); }

void ModuleChild::SetModule(const lldb::ModuleSP &module_sp) {
  m_module_wp = module_sp;
}
