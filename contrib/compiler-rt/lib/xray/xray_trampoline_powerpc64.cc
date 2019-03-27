#include <atomic>
#include <xray/xray_interface.h>

namespace __xray {

extern std::atomic<void (*)(int32_t, XRayEntryType)> XRayPatchedFunction;

// Implement this in C++ instead of assembly, to avoid dealing with ToC by hand.
void CallXRayPatchedFunction(int32_t FuncId, XRayEntryType Type) {
  auto fptr = __xray::XRayPatchedFunction.load();
  if (fptr != nullptr)
    (*fptr)(FuncId, Type);
}

} // namespace __xray
