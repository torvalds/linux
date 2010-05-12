/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** @file ap15rm_clockids.h
    Clock List & string names
*/

/* This is the list of all clock sources available on AP15 and AP20.
 */

// 32 KHz clock - A.K.A relaxation oscillator.
NVRM_CLOCK_SOURCE('C', 'l', 'k', 'S', ' ', ' ', ' ', ' ', ClkS)
// Main clock (crystal or input)
NVRM_CLOCK_SOURCE('C', 'l', 'k', 'M', ' ', ' ', ' ', ' ', ClkM)
// Always double the Clock M
NVRM_CLOCK_SOURCE('C', 'l', 'k', 'D', ' ', ' ', ' ', ' ', ClkD)

// PLL clocks
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'A', '0', ' ', ' ', ' ', PllA0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'A', '1', ' ', ' ', ' ', PllA1)

NVRM_CLOCK_SOURCE('P', 'l', 'l', 'C', '0', ' ', ' ', ' ', PllC0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'C', '1', ' ', ' ', ' ', PllC1)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'D', '0', ' ', ' ', ' ', PllD0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'E', '0', ' ', ' ', ' ', PllE0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'M', '0', ' ', ' ', ' ', PllM0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'M', '1', ' ', ' ', ' ', PllM1)

NVRM_CLOCK_SOURCE('P', 'l', 'l', 'P', '0', ' ', ' ', ' ', PllP0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'P', '1', ' ', ' ', ' ', PllP1)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'P', '2', ' ', ' ', ' ', PllP2)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'P', '3', ' ', ' ', ' ', PllP3)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'P', '4', ' ', ' ', ' ', PllP4)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'S', '0', ' ', ' ', ' ', PllS0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'U', '0', ' ', ' ', ' ', PllU0)
NVRM_CLOCK_SOURCE('P', 'l', 'l', 'X', '0', ' ', ' ', ' ', PllX0)

// External and recovered bit clock sources
NVRM_CLOCK_SOURCE('E', 'x', 't', 'S', 'p', 'd', 'f', ' ', ExtSpdf)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'I', '2', 's', '1', ' ', ExtI2s1)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'I', '2', 's', '2', ' ', ExtI2s2)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'A', 'c', '9', '7', ' ', ExtAc97)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'A', 'u', 'd', 'i', '1', ExtAudio1)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'A', 'u', 'd', 'i', '2', ExtAudio2)
NVRM_CLOCK_SOURCE('E', 'x', 't', 'V', 'i', ' ', ' ', ' ', ExtVi)

// Audio Clocks
NVRM_CLOCK_SOURCE('A', 'u', 'd', 'i', 'S', 'y', 'n', 'c', AudioSync)
NVRM_CLOCK_SOURCE('M', 'p', 'e', 'A', 'u', 'd', 'o', ' ', MpeAudio)

// Internal bus sources
NVRM_CLOCK_SOURCE('C', 'p', 'u', 'B', 'u', 's', ' ', ' ', CpuBus)
NVRM_CLOCK_SOURCE('C', 'p', 'u', 'B', 'r', 'd', 'g', ' ', CpuBridge)
NVRM_CLOCK_SOURCE('S', 'y', 's', 't', 'B', 'u', 's', ' ', SystemBus)
NVRM_CLOCK_SOURCE('A', 'h', 'B', 'u', 's', ' ', ' ', ' ', Ahb)
NVRM_CLOCK_SOURCE('A', 'p', 'B', 'u', 's', ' ', ' ', ' ', Apb)
NVRM_CLOCK_SOURCE('V', 'd', 'e', 'B', 'u', 's', ' ', ' ', Vbus)
