//===- NVPTXUtilities.cpp - Utility Functions -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains miscellaneous utility functions
//===----------------------------------------------------------------------===//

#include "NVPTXUtilities.h"
#include "NVPTX.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MutexGuard.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace llvm {

namespace {
typedef std::map<std::string, std::vector<unsigned> > key_val_pair_t;
typedef std::map<const GlobalValue *, key_val_pair_t> global_val_annot_t;
typedef std::map<const Module *, global_val_annot_t> per_module_annot_t;
} // anonymous namespace

static ManagedStatic<per_module_annot_t> annotationCache;
static sys::Mutex Lock;

void clearAnnotationCache(const Module *Mod) {
  MutexGuard Guard(Lock);
  annotationCache->erase(Mod);
}

static void cacheAnnotationFromMD(const MDNode *md, key_val_pair_t &retval) {
  MutexGuard Guard(Lock);
  assert(md && "Invalid mdnode for annotation");
  assert((md->getNumOperands() % 2) == 1 && "Invalid number of operands");
  // start index = 1, to skip the global variable key
  // increment = 2, to skip the value for each property-value pairs
  for (unsigned i = 1, e = md->getNumOperands(); i != e; i += 2) {
    // property
    const MDString *prop = dyn_cast<MDString>(md->getOperand(i));
    assert(prop && "Annotation property not a string");

    // value
    ConstantInt *Val = mdconst::dyn_extract<ConstantInt>(md->getOperand(i + 1));
    assert(Val && "Value operand not a constant int");

    std::string keyname = prop->getString().str();
    if (retval.find(keyname) != retval.end())
      retval[keyname].push_back(Val->getZExtValue());
    else {
      std::vector<unsigned> tmp;
      tmp.push_back(Val->getZExtValue());
      retval[keyname] = tmp;
    }
  }
}

static void cacheAnnotationFromMD(const Module *m, const GlobalValue *gv) {
  MutexGuard Guard(Lock);
  NamedMDNode *NMD = m->getNamedMetadata("nvvm.annotations");
  if (!NMD)
    return;
  key_val_pair_t tmp;
  for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
    const MDNode *elem = NMD->getOperand(i);

    GlobalValue *entity =
        mdconst::dyn_extract_or_null<GlobalValue>(elem->getOperand(0));
    // entity may be null due to DCE
    if (!entity)
      continue;
    if (entity != gv)
      continue;

    // accumulate annotations for entity in tmp
    cacheAnnotationFromMD(elem, tmp);
  }

  if (tmp.empty()) // no annotations for this gv
    return;

  if ((*annotationCache).find(m) != (*annotationCache).end())
    (*annotationCache)[m][gv] = std::move(tmp);
  else {
    global_val_annot_t tmp1;
    tmp1[gv] = std::move(tmp);
    (*annotationCache)[m] = std::move(tmp1);
  }
}

bool findOneNVVMAnnotation(const GlobalValue *gv, const std::string &prop,
                           unsigned &retval) {
  MutexGuard Guard(Lock);
  const Module *m = gv->getParent();
  if ((*annotationCache).find(m) == (*annotationCache).end())
    cacheAnnotationFromMD(m, gv);
  else if ((*annotationCache)[m].find(gv) == (*annotationCache)[m].end())
    cacheAnnotationFromMD(m, gv);
  if ((*annotationCache)[m][gv].find(prop) == (*annotationCache)[m][gv].end())
    return false;
  retval = (*annotationCache)[m][gv][prop][0];
  return true;
}

bool findAllNVVMAnnotation(const GlobalValue *gv, const std::string &prop,
                           std::vector<unsigned> &retval) {
  MutexGuard Guard(Lock);
  const Module *m = gv->getParent();
  if ((*annotationCache).find(m) == (*annotationCache).end())
    cacheAnnotationFromMD(m, gv);
  else if ((*annotationCache)[m].find(gv) == (*annotationCache)[m].end())
    cacheAnnotationFromMD(m, gv);
  if ((*annotationCache)[m][gv].find(prop) == (*annotationCache)[m][gv].end())
    return false;
  retval = (*annotationCache)[m][gv][prop];
  return true;
}

bool isTexture(const Value &val) {
  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned annot;
    if (findOneNVVMAnnotation(gv, "texture", annot)) {
      assert((annot == 1) && "Unexpected annotation on a texture symbol");
      return true;
    }
  }
  return false;
}

bool isSurface(const Value &val) {
  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned annot;
    if (findOneNVVMAnnotation(gv, "surface", annot)) {
      assert((annot == 1) && "Unexpected annotation on a surface symbol");
      return true;
    }
  }
  return false;
}

bool isSampler(const Value &val) {
  const char *AnnotationName = "sampler";

  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned annot;
    if (findOneNVVMAnnotation(gv, AnnotationName, annot)) {
      assert((annot == 1) && "Unexpected annotation on a sampler symbol");
      return true;
    }
  }
  if (const Argument *arg = dyn_cast<Argument>(&val)) {
    const Function *func = arg->getParent();
    std::vector<unsigned> annot;
    if (findAllNVVMAnnotation(func, AnnotationName, annot)) {
      if (is_contained(annot, arg->getArgNo()))
        return true;
    }
  }
  return false;
}

bool isImageReadOnly(const Value &val) {
  if (const Argument *arg = dyn_cast<Argument>(&val)) {
    const Function *func = arg->getParent();
    std::vector<unsigned> annot;
    if (findAllNVVMAnnotation(func, "rdoimage", annot)) {
      if (is_contained(annot, arg->getArgNo()))
        return true;
    }
  }
  return false;
}

bool isImageWriteOnly(const Value &val) {
  if (const Argument *arg = dyn_cast<Argument>(&val)) {
    const Function *func = arg->getParent();
    std::vector<unsigned> annot;
    if (findAllNVVMAnnotation(func, "wroimage", annot)) {
      if (is_contained(annot, arg->getArgNo()))
        return true;
    }
  }
  return false;
}

bool isImageReadWrite(const Value &val) {
  if (const Argument *arg = dyn_cast<Argument>(&val)) {
    const Function *func = arg->getParent();
    std::vector<unsigned> annot;
    if (findAllNVVMAnnotation(func, "rdwrimage", annot)) {
      if (is_contained(annot, arg->getArgNo()))
        return true;
    }
  }
  return false;
}

bool isImage(const Value &val) {
  return isImageReadOnly(val) || isImageWriteOnly(val) || isImageReadWrite(val);
}

bool isManaged(const Value &val) {
  if(const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned annot;
    if (findOneNVVMAnnotation(gv, "managed", annot)) {
      assert((annot == 1) && "Unexpected annotation on a managed symbol");
      return true;
    }
  }
  return false;
}

std::string getTextureName(const Value &val) {
  assert(val.hasName() && "Found texture variable with no name");
  return val.getName();
}

std::string getSurfaceName(const Value &val) {
  assert(val.hasName() && "Found surface variable with no name");
  return val.getName();
}

std::string getSamplerName(const Value &val) {
  assert(val.hasName() && "Found sampler variable with no name");
  return val.getName();
}

bool getMaxNTIDx(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "maxntidx", x);
}

bool getMaxNTIDy(const Function &F, unsigned &y) {
  return findOneNVVMAnnotation(&F, "maxntidy", y);
}

bool getMaxNTIDz(const Function &F, unsigned &z) {
  return findOneNVVMAnnotation(&F, "maxntidz", z);
}

bool getReqNTIDx(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "reqntidx", x);
}

bool getReqNTIDy(const Function &F, unsigned &y) {
  return findOneNVVMAnnotation(&F, "reqntidy", y);
}

bool getReqNTIDz(const Function &F, unsigned &z) {
  return findOneNVVMAnnotation(&F, "reqntidz", z);
}

bool getMinCTASm(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "minctasm", x);
}

bool getMaxNReg(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "maxnreg", x);
}

bool isKernelFunction(const Function &F) {
  unsigned x = 0;
  bool retval = findOneNVVMAnnotation(&F, "kernel", x);
  if (!retval) {
    // There is no NVVM metadata, check the calling convention
    return F.getCallingConv() == CallingConv::PTX_Kernel;
  }
  return (x == 1);
}

bool getAlign(const Function &F, unsigned index, unsigned &align) {
  std::vector<unsigned> Vs;
  bool retval = findAllNVVMAnnotation(&F, "align", Vs);
  if (!retval)
    return false;
  for (int i = 0, e = Vs.size(); i < e; i++) {
    unsigned v = Vs[i];
    if ((v >> 16) == index) {
      align = v & 0xFFFF;
      return true;
    }
  }
  return false;
}

bool getAlign(const CallInst &I, unsigned index, unsigned &align) {
  if (MDNode *alignNode = I.getMetadata("callalign")) {
    for (int i = 0, n = alignNode->getNumOperands(); i < n; i++) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(alignNode->getOperand(i))) {
        unsigned v = CI->getZExtValue();
        if ((v >> 16) == index) {
          align = v & 0xFFFF;
          return true;
        }
        if ((v >> 16) > index) {
          return false;
        }
      }
    }
  }
  return false;
}

} // namespace llvm
