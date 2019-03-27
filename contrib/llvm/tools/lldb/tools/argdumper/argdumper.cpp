//===-- argdumper.cpp --------------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/JSON.h"
#include "lldb/Utility/StreamString.h"

#include <iostream>

using namespace lldb_private;

int main(int argc, char *argv[]) {
  JSONArray::SP arguments(new JSONArray());
  for (int i = 1; i < argc; i++) {
    arguments->AppendObject(JSONString::SP(new JSONString(argv[i])));
  }

  JSONObject::SP object(new JSONObject());
  object->SetObject("arguments", arguments);

  StreamString ss;

  object->Write(ss);

  std::cout << ss.GetData() << std::endl;

  return 0;
}
