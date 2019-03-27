//===- AMDGPUMetadataVerifier.cpp - MsgPack Types ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements a verifier for AMDGPU HSA metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/AMDGPUMetadataVerifier.h"
#include "llvm/Support/AMDGPUMetadata.h"

namespace llvm {
namespace AMDGPU {
namespace HSAMD {
namespace V3 {

bool MetadataVerifier::verifyScalar(
    msgpack::Node &Node, msgpack::ScalarNode::ScalarKind SKind,
    function_ref<bool(msgpack::ScalarNode &)> verifyValue) {
  auto ScalarPtr = dyn_cast<msgpack::ScalarNode>(&Node);
  if (!ScalarPtr)
    return false;
  auto &Scalar = *ScalarPtr;
  // Do not output extraneous tags for types we know from the spec.
  Scalar.IgnoreTag = true;
  if (Scalar.getScalarKind() != SKind) {
    if (Strict)
      return false;
    // If we are not strict, we interpret string values as "implicitly typed"
    // and attempt to coerce them to the expected type here.
    if (Scalar.getScalarKind() != msgpack::ScalarNode::SK_String)
      return false;
    std::string StringValue = Scalar.getString();
    Scalar.setScalarKind(SKind);
    if (Scalar.inputYAML(StringValue) != StringRef())
      return false;
  }
  if (verifyValue)
    return verifyValue(Scalar);
  return true;
}

bool MetadataVerifier::verifyInteger(msgpack::Node &Node) {
  if (!verifyScalar(Node, msgpack::ScalarNode::SK_UInt))
    if (!verifyScalar(Node, msgpack::ScalarNode::SK_Int))
      return false;
  return true;
}

bool MetadataVerifier::verifyArray(
    msgpack::Node &Node, function_ref<bool(msgpack::Node &)> verifyNode,
    Optional<size_t> Size) {
  auto ArrayPtr = dyn_cast<msgpack::ArrayNode>(&Node);
  if (!ArrayPtr)
    return false;
  auto &Array = *ArrayPtr;
  if (Size && Array.size() != *Size)
    return false;
  for (auto &Item : Array)
    if (!verifyNode(*Item.get()))
      return false;

  return true;
}

bool MetadataVerifier::verifyEntry(
    msgpack::MapNode &MapNode, StringRef Key, bool Required,
    function_ref<bool(msgpack::Node &)> verifyNode) {
  auto Entry = MapNode.find(Key);
  if (Entry == MapNode.end())
    return !Required;
  return verifyNode(*Entry->second.get());
}

bool MetadataVerifier::verifyScalarEntry(
    msgpack::MapNode &MapNode, StringRef Key, bool Required,
    msgpack::ScalarNode::ScalarKind SKind,
    function_ref<bool(msgpack::ScalarNode &)> verifyValue) {
  return verifyEntry(MapNode, Key, Required, [=](msgpack::Node &Node) {
    return verifyScalar(Node, SKind, verifyValue);
  });
}

bool MetadataVerifier::verifyIntegerEntry(msgpack::MapNode &MapNode,
                                          StringRef Key, bool Required) {
  return verifyEntry(MapNode, Key, Required, [this](msgpack::Node &Node) {
    return verifyInteger(Node);
  });
}

bool MetadataVerifier::verifyKernelArgs(msgpack::Node &Node) {
  auto ArgsMapPtr = dyn_cast<msgpack::MapNode>(&Node);
  if (!ArgsMapPtr)
    return false;
  auto &ArgsMap = *ArgsMapPtr;

  if (!verifyScalarEntry(ArgsMap, ".name", false,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".type_name", false,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".size", true))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".offset", true))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".value_kind", true,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("by_value", true)
                               .Case("global_buffer", true)
                               .Case("dynamic_shared_pointer", true)
                               .Case("sampler", true)
                               .Case("image", true)
                               .Case("pipe", true)
                               .Case("queue", true)
                               .Case("hidden_global_offset_x", true)
                               .Case("hidden_global_offset_y", true)
                               .Case("hidden_global_offset_z", true)
                               .Case("hidden_none", true)
                               .Case("hidden_printf_buffer", true)
                               .Case("hidden_default_queue", true)
                               .Case("hidden_completion_action", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".value_type", true,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("struct", true)
                               .Case("i8", true)
                               .Case("u8", true)
                               .Case("i16", true)
                               .Case("u16", true)
                               .Case("f16", true)
                               .Case("i32", true)
                               .Case("u32", true)
                               .Case("f32", true)
                               .Case("i64", true)
                               .Case("u64", true)
                               .Case("f64", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyIntegerEntry(ArgsMap, ".pointee_align", false))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".address_space", false,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("private", true)
                               .Case("global", true)
                               .Case("constant", true)
                               .Case("local", true)
                               .Case("generic", true)
                               .Case("region", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".access", false,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("read_only", true)
                               .Case("write_only", true)
                               .Case("read_write", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".actual_access", false,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("read_only", true)
                               .Case("write_only", true)
                               .Case("read_write", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_const", false,
                         msgpack::ScalarNode::SK_Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_restrict", false,
                         msgpack::ScalarNode::SK_Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_volatile", false,
                         msgpack::ScalarNode::SK_Boolean))
    return false;
  if (!verifyScalarEntry(ArgsMap, ".is_pipe", false,
                         msgpack::ScalarNode::SK_Boolean))
    return false;

  return true;
}

bool MetadataVerifier::verifyKernel(msgpack::Node &Node) {
  auto KernelMapPtr = dyn_cast<msgpack::MapNode>(&Node);
  if (!KernelMapPtr)
    return false;
  auto &KernelMap = *KernelMapPtr;

  if (!verifyScalarEntry(KernelMap, ".name", true,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".symbol", true,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".language", false,
                         msgpack::ScalarNode::SK_String,
                         [](msgpack::ScalarNode &SNode) {
                           return StringSwitch<bool>(SNode.getString())
                               .Case("OpenCL C", true)
                               .Case("OpenCL C++", true)
                               .Case("HCC", true)
                               .Case("HIP", true)
                               .Case("OpenMP", true)
                               .Case("Assembler", true)
                               .Default(false);
                         }))
    return false;
  if (!verifyEntry(
          KernelMap, ".language_version", false, [this](msgpack::Node &Node) {
            return verifyArray(
                Node,
                [this](msgpack::Node &Node) { return verifyInteger(Node); }, 2);
          }))
    return false;
  if (!verifyEntry(KernelMap, ".args", false, [this](msgpack::Node &Node) {
        return verifyArray(Node, [this](msgpack::Node &Node) {
          return verifyKernelArgs(Node);
        });
      }))
    return false;
  if (!verifyEntry(KernelMap, ".reqd_workgroup_size", false,
                   [this](msgpack::Node &Node) {
                     return verifyArray(Node,
                                        [this](msgpack::Node &Node) {
                                          return verifyInteger(Node);
                                        },
                                        3);
                   }))
    return false;
  if (!verifyEntry(KernelMap, ".workgroup_size_hint", false,
                   [this](msgpack::Node &Node) {
                     return verifyArray(Node,
                                        [this](msgpack::Node &Node) {
                                          return verifyInteger(Node);
                                        },
                                        3);
                   }))
    return false;
  if (!verifyScalarEntry(KernelMap, ".vec_type_hint", false,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyScalarEntry(KernelMap, ".device_enqueue_symbol", false,
                         msgpack::ScalarNode::SK_String))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".kernarg_segment_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".group_segment_fixed_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".private_segment_fixed_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".kernarg_segment_align", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".wavefront_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".sgpr_count", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".vgpr_count", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".max_flat_workgroup_size", true))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".sgpr_spill_count", false))
    return false;
  if (!verifyIntegerEntry(KernelMap, ".vgpr_spill_count", false))
    return false;

  return true;
}

bool MetadataVerifier::verify(msgpack::Node &HSAMetadataRoot) {
  auto RootMapPtr = dyn_cast<msgpack::MapNode>(&HSAMetadataRoot);
  if (!RootMapPtr)
    return false;
  auto &RootMap = *RootMapPtr;

  if (!verifyEntry(
          RootMap, "amdhsa.version", true, [this](msgpack::Node &Node) {
            return verifyArray(
                Node,
                [this](msgpack::Node &Node) { return verifyInteger(Node); }, 2);
          }))
    return false;
  if (!verifyEntry(
          RootMap, "amdhsa.printf", false, [this](msgpack::Node &Node) {
            return verifyArray(Node, [this](msgpack::Node &Node) {
              return verifyScalar(Node, msgpack::ScalarNode::SK_String);
            });
          }))
    return false;
  if (!verifyEntry(RootMap, "amdhsa.kernels", true,
                   [this](msgpack::Node &Node) {
                     return verifyArray(Node, [this](msgpack::Node &Node) {
                       return verifyKernel(Node);
                     });
                   }))
    return false;

  return true;
}

} // end namespace V3
} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm
