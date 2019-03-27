//===- JSONBackend.cpp - Generate a JSON dump of all records. -*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This TableGen back end generates a machine-readable representation
// of all the classes and records defined by the input, in JSON format.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include "llvm/Support/JSON.h"

#define DEBUG_TYPE "json-emitter"

using namespace llvm;

namespace {

class JSONEmitter {
private:
  RecordKeeper &Records;

  json::Value translateInit(const Init &I);
  json::Array listSuperclasses(const Record &R);

public:
  JSONEmitter(RecordKeeper &R);

  void run(raw_ostream &OS);
};

} // end anonymous namespace

JSONEmitter::JSONEmitter(RecordKeeper &R) : Records(R) {}

json::Value JSONEmitter::translateInit(const Init &I) {

  // Init subclasses that we return as JSON primitive values of one
  // kind or another.

  if (isa<UnsetInit>(&I)) {
    return nullptr;
  } else if (auto *Bit = dyn_cast<BitInit>(&I)) {
    return Bit->getValue() ? 1 : 0;
  } else if (auto *Bits = dyn_cast<BitsInit>(&I)) {
    json::Array array;
    for (unsigned i = 0, limit = Bits->getNumBits(); i < limit; i++)
      array.push_back(translateInit(*Bits->getBit(i)));
    return std::move(array);
  } else if (auto *Int = dyn_cast<IntInit>(&I)) {
    return Int->getValue();
  } else if (auto *Str = dyn_cast<StringInit>(&I)) {
    return Str->getValue();
  } else if (auto *Code = dyn_cast<CodeInit>(&I)) {
    return Code->getValue();
  } else if (auto *List = dyn_cast<ListInit>(&I)) {
    json::Array array;
    for (auto val : *List)
      array.push_back(translateInit(*val));
    return std::move(array);
  }

  // Init subclasses that we return as JSON objects containing a
  // 'kind' discriminator. For these, we also provide the same
  // translation back into TableGen input syntax that -print-records
  // would give.

  json::Object obj;
  obj["printable"] = I.getAsString();

  if (auto *Def = dyn_cast<DefInit>(&I)) {
    obj["kind"] = "def";
    obj["def"] = Def->getDef()->getName();
    return std::move(obj);
  } else if (auto *Var = dyn_cast<VarInit>(&I)) {
    obj["kind"] = "var";
    obj["var"] = Var->getName();
    return std::move(obj);
  } else if (auto *VarBit = dyn_cast<VarBitInit>(&I)) {
    if (auto *Var = dyn_cast<VarInit>(VarBit->getBitVar())) {
      obj["kind"] = "varbit";
      obj["var"] = Var->getName();
      obj["index"] = VarBit->getBitNum();
      return std::move(obj);
    }
  } else if (auto *Dag = dyn_cast<DagInit>(&I)) {
    obj["kind"] = "dag";
    obj["operator"] = translateInit(*Dag->getOperator());
    if (auto name = Dag->getName())
      obj["name"] = name->getAsUnquotedString();
    json::Array args;
    for (unsigned i = 0, limit = Dag->getNumArgs(); i < limit; ++i) {
      json::Array arg;
      arg.push_back(translateInit(*Dag->getArg(i)));
      if (auto argname = Dag->getArgName(i))
        arg.push_back(argname->getAsUnquotedString());
      else
        arg.push_back(nullptr);
      args.push_back(std::move(arg));
    }
    obj["args"] = std::move(args);
    return std::move(obj);
  }

  // Final fallback: anything that gets past here is simply given a
  // kind field of 'complex', and the only other field is the standard
  // 'printable' representation.

  assert(!I.isConcrete());
  obj["kind"] = "complex";
  return std::move(obj);
}

void JSONEmitter::run(raw_ostream &OS) {
  json::Object root;

  root["!tablegen_json_version"] = 1;

  // Prepare the arrays that will list the instances of every class.
  // We mostly fill those in by iterating over the superclasses of
  // each def, but we also want to ensure we store an empty list for a
  // class with no instances at all, so we do a preliminary iteration
  // over the classes, invoking std::map::operator[] to default-
  // construct the array for each one.
  std::map<std::string, json::Array> instance_lists;
  for (const auto &C : Records.getClasses()) {
    auto &Name = C.second->getNameInitAsString();
    (void)instance_lists[Name];
  }

  // Main iteration over the defs.
  for (const auto &D : Records.getDefs()) {
    auto &Name = D.second->getNameInitAsString();
    auto &Def = *D.second;

    json::Object obj;
    json::Array fields;

    for (const RecordVal &RV : Def.getValues()) {
      if (!Def.isTemplateArg(RV.getNameInit())) {
        auto Name = RV.getNameInitAsString();
        if (RV.getPrefix())
          fields.push_back(Name);
        obj[Name] = translateInit(*RV.getValue());
      }
    }

    obj["!fields"] = std::move(fields);

    json::Array superclasses;
    for (const auto &SuperPair : Def.getSuperClasses())
      superclasses.push_back(SuperPair.first->getNameInitAsString());
    obj["!superclasses"] = std::move(superclasses);

    obj["!name"] = Name;
    obj["!anonymous"] = Def.isAnonymous();

    root[Name] = std::move(obj);

    // Add this def to the instance list for each of its superclasses.
    for (const auto &SuperPair : Def.getSuperClasses()) {
      auto SuperName = SuperPair.first->getNameInitAsString();
      instance_lists[SuperName].push_back(Name);
    }
  }

  // Make a JSON object from the std::map of instance lists.
  json::Object instanceof;
  for (auto kv: instance_lists)
    instanceof[kv.first] = std::move(kv.second);
  root["!instanceof"] = std::move(instanceof);

  // Done. Write the output.
  OS << json::Value(std::move(root)) << "\n";
}

namespace llvm {

void EmitJSON(RecordKeeper &RK, raw_ostream &OS) { JSONEmitter(RK).run(OS); }
} // end namespace llvm
