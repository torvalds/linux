//===--- AVR.cpp - Implement AVR target feature support -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements AVR TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

namespace clang {
namespace targets {

/// Information about a specific microcontroller.
struct LLVM_LIBRARY_VISIBILITY MCUInfo {
  const char *Name;
  const char *DefineName;
};

// This list should be kept up-to-date with AVRDevices.td in LLVM.
static MCUInfo AVRMcus[] = {
    {"at90s1200", "__AVR_AT90S1200__"},
    {"attiny11", "__AVR_ATtiny11__"},
    {"attiny12", "__AVR_ATtiny12__"},
    {"attiny15", "__AVR_ATtiny15__"},
    {"attiny28", "__AVR_ATtiny28__"},
    {"at90s2313", "__AVR_AT90S2313__"},
    {"at90s2323", "__AVR_AT90S2323__"},
    {"at90s2333", "__AVR_AT90S2333__"},
    {"at90s2343", "__AVR_AT90S2343__"},
    {"attiny22", "__AVR_ATtiny22__"},
    {"attiny26", "__AVR_ATtiny26__"},
    {"at86rf401", "__AVR_AT86RF401__"},
    {"at90s4414", "__AVR_AT90S4414__"},
    {"at90s4433", "__AVR_AT90S4433__"},
    {"at90s4434", "__AVR_AT90S4434__"},
    {"at90s8515", "__AVR_AT90S8515__"},
    {"at90c8534", "__AVR_AT90c8534__"},
    {"at90s8535", "__AVR_AT90S8535__"},
    {"ata5272", "__AVR_ATA5272__"},
    {"attiny13", "__AVR_ATtiny13__"},
    {"attiny13a", "__AVR_ATtiny13A__"},
    {"attiny2313", "__AVR_ATtiny2313__"},
    {"attiny2313a", "__AVR_ATtiny2313A__"},
    {"attiny24", "__AVR_ATtiny24__"},
    {"attiny24a", "__AVR_ATtiny24A__"},
    {"attiny4313", "__AVR_ATtiny4313__"},
    {"attiny44", "__AVR_ATtiny44__"},
    {"attiny44a", "__AVR_ATtiny44A__"},
    {"attiny84", "__AVR_ATtiny84__"},
    {"attiny84a", "__AVR_ATtiny84A__"},
    {"attiny25", "__AVR_ATtiny25__"},
    {"attiny45", "__AVR_ATtiny45__"},
    {"attiny85", "__AVR_ATtiny85__"},
    {"attiny261", "__AVR_ATtiny261__"},
    {"attiny261a", "__AVR_ATtiny261A__"},
    {"attiny461", "__AVR_ATtiny461__"},
    {"attiny461a", "__AVR_ATtiny461A__"},
    {"attiny861", "__AVR_ATtiny861__"},
    {"attiny861a", "__AVR_ATtiny861A__"},
    {"attiny87", "__AVR_ATtiny87__"},
    {"attiny43u", "__AVR_ATtiny43U__"},
    {"attiny48", "__AVR_ATtiny48__"},
    {"attiny88", "__AVR_ATtiny88__"},
    {"attiny828", "__AVR_ATtiny828__"},
    {"at43usb355", "__AVR_AT43USB355__"},
    {"at76c711", "__AVR_AT76C711__"},
    {"atmega103", "__AVR_ATmega103__"},
    {"at43usb320", "__AVR_AT43USB320__"},
    {"attiny167", "__AVR_ATtiny167__"},
    {"at90usb82", "__AVR_AT90USB82__"},
    {"at90usb162", "__AVR_AT90USB162__"},
    {"ata5505", "__AVR_ATA5505__"},
    {"atmega8u2", "__AVR_ATmega8U2__"},
    {"atmega16u2", "__AVR_ATmega16U2__"},
    {"atmega32u2", "__AVR_ATmega32U2__"},
    {"attiny1634", "__AVR_ATtiny1634__"},
    {"atmega8", "__AVR_ATmega8__"},
    {"ata6289", "__AVR_ATA6289__"},
    {"atmega8a", "__AVR_ATmega8A__"},
    {"ata6285", "__AVR_ATA6285__"},
    {"ata6286", "__AVR_ATA6286__"},
    {"atmega48", "__AVR_ATmega48__"},
    {"atmega48a", "__AVR_ATmega48A__"},
    {"atmega48pa", "__AVR_ATmega48PA__"},
    {"atmega48p", "__AVR_ATmega48P__"},
    {"atmega88", "__AVR_ATmega88__"},
    {"atmega88a", "__AVR_ATmega88A__"},
    {"atmega88p", "__AVR_ATmega88P__"},
    {"atmega88pa", "__AVR_ATmega88PA__"},
    {"atmega8515", "__AVR_ATmega8515__"},
    {"atmega8535", "__AVR_ATmega8535__"},
    {"atmega8hva", "__AVR_ATmega8HVA__"},
    {"at90pwm1", "__AVR_AT90PWM1__"},
    {"at90pwm2", "__AVR_AT90PWM2__"},
    {"at90pwm2b", "__AVR_AT90PWM2B__"},
    {"at90pwm3", "__AVR_AT90PWM3__"},
    {"at90pwm3b", "__AVR_AT90PWM3B__"},
    {"at90pwm81", "__AVR_AT90PWM81__"},
    {"ata5790", "__AVR_ATA5790__"},
    {"ata5795", "__AVR_ATA5795__"},
    {"atmega16", "__AVR_ATmega16__"},
    {"atmega16a", "__AVR_ATmega16A__"},
    {"atmega161", "__AVR_ATmega161__"},
    {"atmega162", "__AVR_ATmega162__"},
    {"atmega163", "__AVR_ATmega163__"},
    {"atmega164a", "__AVR_ATmega164A__"},
    {"atmega164p", "__AVR_ATmega164P__"},
    {"atmega164pa", "__AVR_ATmega164PA__"},
    {"atmega165", "__AVR_ATmega165__"},
    {"atmega165a", "__AVR_ATmega165A__"},
    {"atmega165p", "__AVR_ATmega165P__"},
    {"atmega165pa", "__AVR_ATmega165PA__"},
    {"atmega168", "__AVR_ATmega168__"},
    {"atmega168a", "__AVR_ATmega168A__"},
    {"atmega168p", "__AVR_ATmega168P__"},
    {"atmega168pa", "__AVR_ATmega168PA__"},
    {"atmega169", "__AVR_ATmega169__"},
    {"atmega169a", "__AVR_ATmega169A__"},
    {"atmega169p", "__AVR_ATmega169P__"},
    {"atmega169pa", "__AVR_ATmega169PA__"},
    {"atmega32", "__AVR_ATmega32__"},
    {"atmega32a", "__AVR_ATmega32A__"},
    {"atmega323", "__AVR_ATmega323__"},
    {"atmega324a", "__AVR_ATmega324A__"},
    {"atmega324p", "__AVR_ATmega324P__"},
    {"atmega324pa", "__AVR_ATmega324PA__"},
    {"atmega325", "__AVR_ATmega325__"},
    {"atmega325a", "__AVR_ATmega325A__"},
    {"atmega325p", "__AVR_ATmega325P__"},
    {"atmega325pa", "__AVR_ATmega325PA__"},
    {"atmega3250", "__AVR_ATmega3250__"},
    {"atmega3250a", "__AVR_ATmega3250A__"},
    {"atmega3250p", "__AVR_ATmega3250P__"},
    {"atmega3250pa", "__AVR_ATmega3250PA__"},
    {"atmega328", "__AVR_ATmega328__"},
    {"atmega328p", "__AVR_ATmega328P__"},
    {"atmega329", "__AVR_ATmega329__"},
    {"atmega329a", "__AVR_ATmega329A__"},
    {"atmega329p", "__AVR_ATmega329P__"},
    {"atmega329pa", "__AVR_ATmega329PA__"},
    {"atmega3290", "__AVR_ATmega3290__"},
    {"atmega3290a", "__AVR_ATmega3290A__"},
    {"atmega3290p", "__AVR_ATmega3290P__"},
    {"atmega3290pa", "__AVR_ATmega3290PA__"},
    {"atmega406", "__AVR_ATmega406__"},
    {"atmega64", "__AVR_ATmega64__"},
    {"atmega64a", "__AVR_ATmega64A__"},
    {"atmega640", "__AVR_ATmega640__"},
    {"atmega644", "__AVR_ATmega644__"},
    {"atmega644a", "__AVR_ATmega644A__"},
    {"atmega644p", "__AVR_ATmega644P__"},
    {"atmega644pa", "__AVR_ATmega644PA__"},
    {"atmega645", "__AVR_ATmega645__"},
    {"atmega645a", "__AVR_ATmega645A__"},
    {"atmega645p", "__AVR_ATmega645P__"},
    {"atmega649", "__AVR_ATmega649__"},
    {"atmega649a", "__AVR_ATmega649A__"},
    {"atmega649p", "__AVR_ATmega649P__"},
    {"atmega6450", "__AVR_ATmega6450__"},
    {"atmega6450a", "__AVR_ATmega6450A__"},
    {"atmega6450p", "__AVR_ATmega6450P__"},
    {"atmega6490", "__AVR_ATmega6490__"},
    {"atmega6490a", "__AVR_ATmega6490A__"},
    {"atmega6490p", "__AVR_ATmega6490P__"},
    {"atmega64rfr2", "__AVR_ATmega64RFR2__"},
    {"atmega644rfr2", "__AVR_ATmega644RFR2__"},
    {"atmega16hva", "__AVR_ATmega16HVA__"},
    {"atmega16hva2", "__AVR_ATmega16HVA2__"},
    {"atmega16hvb", "__AVR_ATmega16HVB__"},
    {"atmega16hvbrevb", "__AVR_ATmega16HVBREVB__"},
    {"atmega32hvb", "__AVR_ATmega32HVB__"},
    {"atmega32hvbrevb", "__AVR_ATmega32HVBREVB__"},
    {"atmega64hve", "__AVR_ATmega64HVE__"},
    {"at90can32", "__AVR_AT90CAN32__"},
    {"at90can64", "__AVR_AT90CAN64__"},
    {"at90pwm161", "__AVR_AT90PWM161__"},
    {"at90pwm216", "__AVR_AT90PWM216__"},
    {"at90pwm316", "__AVR_AT90PWM316__"},
    {"atmega32c1", "__AVR_ATmega32C1__"},
    {"atmega64c1", "__AVR_ATmega64C1__"},
    {"atmega16m1", "__AVR_ATmega16M1__"},
    {"atmega32m1", "__AVR_ATmega32M1__"},
    {"atmega64m1", "__AVR_ATmega64M1__"},
    {"atmega16u4", "__AVR_ATmega16U4__"},
    {"atmega32u4", "__AVR_ATmega32U4__"},
    {"atmega32u6", "__AVR_ATmega32U6__"},
    {"at90usb646", "__AVR_AT90USB646__"},
    {"at90usb647", "__AVR_AT90USB647__"},
    {"at90scr100", "__AVR_AT90SCR100__"},
    {"at94k", "__AVR_AT94K__"},
    {"m3000", "__AVR_AT000__"},
    {"atmega128", "__AVR_ATmega128__"},
    {"atmega128a", "__AVR_ATmega128A__"},
    {"atmega1280", "__AVR_ATmega1280__"},
    {"atmega1281", "__AVR_ATmega1281__"},
    {"atmega1284", "__AVR_ATmega1284__"},
    {"atmega1284p", "__AVR_ATmega1284P__"},
    {"atmega128rfa1", "__AVR_ATmega128RFA1__"},
    {"atmega128rfr2", "__AVR_ATmega128RFR2__"},
    {"atmega1284rfr2", "__AVR_ATmega1284RFR2__"},
    {"at90can128", "__AVR_AT90CAN128__"},
    {"at90usb1286", "__AVR_AT90USB1286__"},
    {"at90usb1287", "__AVR_AT90USB1287__"},
    {"atmega2560", "__AVR_ATmega2560__"},
    {"atmega2561", "__AVR_ATmega2561__"},
    {"atmega256rfr2", "__AVR_ATmega256RFR2__"},
    {"atmega2564rfr2", "__AVR_ATmega2564RFR2__"},
    {"atxmega16a4", "__AVR_ATxmega16A4__"},
    {"atxmega16a4u", "__AVR_ATxmega16a4U__"},
    {"atxmega16c4", "__AVR_ATxmega16C4__"},
    {"atxmega16d4", "__AVR_ATxmega16D4__"},
    {"atxmega32a4", "__AVR_ATxmega32A4__"},
    {"atxmega32a4u", "__AVR_ATxmega32A4U__"},
    {"atxmega32c4", "__AVR_ATxmega32C4__"},
    {"atxmega32d4", "__AVR_ATxmega32D4__"},
    {"atxmega32e5", "__AVR_ATxmega32E5__"},
    {"atxmega16e5", "__AVR_ATxmega16E5__"},
    {"atxmega8e5", "__AVR_ATxmega8E5__"},
    {"atxmega32x1", "__AVR_ATxmega32X1__"},
    {"atxmega64a3", "__AVR_ATxmega64A3__"},
    {"atxmega64a3u", "__AVR_ATxmega64A3U__"},
    {"atxmega64a4u", "__AVR_ATxmega64A4U__"},
    {"atxmega64b1", "__AVR_ATxmega64B1__"},
    {"atxmega64b3", "__AVR_ATxmega64B3__"},
    {"atxmega64c3", "__AVR_ATxmega64C3__"},
    {"atxmega64d3", "__AVR_ATxmega64D3__"},
    {"atxmega64d4", "__AVR_ATxmega64D4__"},
    {"atxmega64a1", "__AVR_ATxmega64A1__"},
    {"atxmega64a1u", "__AVR_ATxmega64A1U__"},
    {"atxmega128a3", "__AVR_ATxmega128A3__"},
    {"atxmega128a3u", "__AVR_ATxmega128A3U__"},
    {"atxmega128b1", "__AVR_ATxmega128B1__"},
    {"atxmega128b3", "__AVR_ATxmega128B3__"},
    {"atxmega128c3", "__AVR_ATxmega128C3__"},
    {"atxmega128d3", "__AVR_ATxmega128D3__"},
    {"atxmega128d4", "__AVR_ATxmega128D4__"},
    {"atxmega192a3", "__AVR_ATxmega192A3__"},
    {"atxmega192a3u", "__AVR_ATxmega192A3U__"},
    {"atxmega192c3", "__AVR_ATxmega192C3__"},
    {"atxmega192d3", "__AVR_ATxmega192D3__"},
    {"atxmega256a3", "__AVR_ATxmega256A3__"},
    {"atxmega256a3u", "__AVR_ATxmega256A3U__"},
    {"atxmega256a3b", "__AVR_ATxmega256A3B__"},
    {"atxmega256a3bu", "__AVR_ATxmega256A3BU__"},
    {"atxmega256c3", "__AVR_ATxmega256C3__"},
    {"atxmega256d3", "__AVR_ATxmega256D3__"},
    {"atxmega384c3", "__AVR_ATxmega384C3__"},
    {"atxmega384d3", "__AVR_ATxmega384D3__"},
    {"atxmega128a1", "__AVR_ATxmega128A1__"},
    {"atxmega128a1u", "__AVR_ATxmega128A1U__"},
    {"atxmega128a4u", "__AVR_ATxmega128a4U__"},
    {"attiny4", "__AVR_ATtiny4__"},
    {"attiny5", "__AVR_ATtiny5__"},
    {"attiny9", "__AVR_ATtiny9__"},
    {"attiny10", "__AVR_ATtiny10__"},
    {"attiny20", "__AVR_ATtiny20__"},
    {"attiny40", "__AVR_ATtiny40__"},
    {"attiny102", "__AVR_ATtiny102__"},
    {"attiny104", "__AVR_ATtiny104__"},
};

} // namespace targets
} // namespace clang

static constexpr llvm::StringLiteral ValidFamilyNames[] = {
    "avr1",      "avr2",      "avr25",     "avr3",      "avr31",
    "avr35",     "avr4",      "avr5",      "avr51",     "avr6",
    "avrxmega1", "avrxmega2", "avrxmega3", "avrxmega4", "avrxmega5",
    "avrxmega6", "avrxmega7", "avrtiny"};

bool AVRTargetInfo::isValidCPUName(StringRef Name) const {
  bool IsFamily =
      llvm::find(ValidFamilyNames, Name) != std::end(ValidFamilyNames);

  bool IsMCU =
      llvm::find_if(AVRMcus, [&](const MCUInfo &Info) {
        return Info.Name == Name;
      }) != std::end(AVRMcus);
  return IsFamily || IsMCU;
}

void AVRTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidFamilyNames), std::end(ValidFamilyNames));
  for (const MCUInfo &Info : AVRMcus)
    Values.push_back(Info.Name);
}

void AVRTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("AVR");
  Builder.defineMacro("__AVR");
  Builder.defineMacro("__AVR__");

  if (!this->CPU.empty()) {
    auto It = llvm::find_if(
        AVRMcus, [&](const MCUInfo &Info) { return Info.Name == this->CPU; });

    if (It != std::end(AVRMcus))
      Builder.defineMacro(It->DefineName);
  }
}
