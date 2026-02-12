// SPDX-License-Identifier: GPL-2.0

//! `register!` macro to define register layout and accessors.
//!
//! A single register typically includes several fields, which are accessed through a combination
//! of bit-shift and mask operations that introduce a class of potential mistakes, notably because
//! not all possible field values are necessarily valid.
//!
//! The `register!` macro in this module provides an intuitive and readable syntax for defining a
//! dedicated type for each register. Each such type comes with its own field accessors that can
//! return an error if a field's value is invalid. Please look at the [`bitfield`] macro for the
//! complete syntax of fields definitions.

/// Trait providing a base address to be added to the offset of a relative register to obtain
/// its actual offset.
///
/// The `T` generic argument is used to distinguish which base to use, in case a type provides
/// several bases. It is given to the `register!` macro to restrict the use of the register to
/// implementors of this particular variant.
pub(crate) trait RegisterBase<T> {
    const BASE: usize;
}

/// Defines a dedicated type for a register with an absolute offset, including getter and setter
/// methods for its fields and methods to read and write it from an `Io` region.
///
/// Example:
///
/// ```no_run
/// register!(BOOT_0 @ 0x00000100, "Basic revision information about the GPU" {
///    3:0     minor_revision as u8, "Minor revision of the chip";
///    7:4     major_revision as u8, "Major revision of the chip";
///    28:20   chipset as u32 ?=> Chipset, "Chipset model";
/// });
/// ```
///
/// This defines a `BOOT_0` type which can be read or written from offset `0x100` of an `Io`
/// region. It is composed of 3 fields, for instance `minor_revision` is made of the 4 least
/// significant bits of the register. Each field can be accessed and modified using accessor
/// methods:
///
/// ```no_run
/// // Read from the register's defined offset (0x100).
/// let boot0 = BOOT_0::read(&bar);
/// pr_info!("chip revision: {}.{}", boot0.major_revision(), boot0.minor_revision());
///
/// // `Chipset::try_from` is called with the value of the `chipset` field and returns an
/// // error if it is invalid.
/// let chipset = boot0.chipset()?;
///
/// // Update some fields and write the value back.
/// boot0.set_major_revision(3).set_minor_revision(10).write(&bar);
///
/// // Or, just read and update the register in a single step:
/// BOOT_0::update(&bar, |r| r.set_major_revision(3).set_minor_revision(10));
/// ```
///
/// The documentation strings are optional. If present, they will be added to the type's
/// definition, or the field getter and setter methods they are attached to.
///
/// It is also possible to create a alias register by using the `=> ALIAS` syntax. This is useful
/// for cases where a register's interpretation depends on the context:
///
/// ```no_run
/// register!(SCRATCH @ 0x00000200, "Scratch register" {
///    31:0     value as u32, "Raw value";
/// });
///
/// register!(SCRATCH_BOOT_STATUS => SCRATCH, "Boot status of the firmware" {
///     0:0     completed as bool, "Whether the firmware has completed booting";
/// });
/// ```
///
/// In this example, `SCRATCH_0_BOOT_STATUS` uses the same I/O address as `SCRATCH`, while also
/// providing its own `completed` field.
///
/// ## Relative registers
///
/// A register can be defined as being accessible from a fixed offset of a provided base. For
/// instance, imagine the following I/O space:
///
/// ```text
///           +-----------------------------+
///           |             ...             |
///           |                             |
///  0x100--->+------------CPU0-------------+
///           |                             |
///  0x110--->+-----------------------------+
///           |           CPU_CTL           |
///           +-----------------------------+
///           |             ...             |
///           |                             |
///           |                             |
///  0x200--->+------------CPU1-------------+
///           |                             |
///  0x210--->+-----------------------------+
///           |           CPU_CTL           |
///           +-----------------------------+
///           |             ...             |
///           +-----------------------------+
/// ```
///
/// `CPU0` and `CPU1` both have a `CPU_CTL` register that starts at offset `0x10` of their I/O
/// space segment. Since both instances of `CPU_CTL` share the same layout, we don't want to define
/// them twice and would prefer a way to select which one to use from a single definition
///
/// This can be done using the `Base[Offset]` syntax when specifying the register's address.
///
/// `Base` is an arbitrary type (typically a ZST) to be used as a generic parameter of the
/// [`RegisterBase`] trait to provide the base as a constant, i.e. each type providing a base for
/// this register needs to implement `RegisterBase<Base>`. Here is the above example translated
/// into code:
///
/// ```no_run
/// // Type used to identify the base.
/// pub(crate) struct CpuCtlBase;
///
/// // ZST describing `CPU0`.
/// struct Cpu0;
/// impl RegisterBase<CpuCtlBase> for Cpu0 {
///     const BASE: usize = 0x100;
/// }
/// // Singleton of `CPU0` used to identify it.
/// const CPU0: Cpu0 = Cpu0;
///
/// // ZST describing `CPU1`.
/// struct Cpu1;
/// impl RegisterBase<CpuCtlBase> for Cpu1 {
///     const BASE: usize = 0x200;
/// }
/// // Singleton of `CPU1` used to identify it.
/// const CPU1: Cpu1 = Cpu1;
///
/// // This makes `CPU_CTL` accessible from all implementors of `RegisterBase<CpuCtlBase>`.
/// register!(CPU_CTL @ CpuCtlBase[0x10], "CPU core control" {
///     0:0     start as bool, "Start the CPU core";
/// });
///
/// // The `read`, `write` and `update` methods of relative registers take an extra `base` argument
/// // that is used to resolve its final address by adding its `BASE` to the offset of the
/// // register.
///
/// // Start `CPU0`.
/// CPU_CTL::update(bar, &CPU0, |r| r.set_start(true));
///
/// // Start `CPU1`.
/// CPU_CTL::update(bar, &CPU1, |r| r.set_start(true));
///
/// // Aliases can also be defined for relative register.
/// register!(CPU_CTL_ALIAS => CpuCtlBase[CPU_CTL], "Alias to CPU core control" {
///     1:1     alias_start as bool, "Start the aliased CPU core";
/// });
///
/// // Start the aliased `CPU0`.
/// CPU_CTL_ALIAS::update(bar, &CPU0, |r| r.set_alias_start(true));
/// ```
///
/// ## Arrays of registers
///
/// Some I/O areas contain consecutive values that can be interpreted in the same way. These areas
/// can be defined as an array of identical registers, allowing them to be accessed by index with
/// compile-time or runtime bound checking. Simply define their address as `Address[Size]`, and add
/// an `idx` parameter to their `read`, `write` and `update` methods:
///
/// ```no_run
/// # fn no_run() -> Result<(), Error> {
/// # fn get_scratch_idx() -> usize {
/// #   0x15
/// # }
/// // Array of 64 consecutive registers with the same layout starting at offset `0x80`.
/// register!(SCRATCH @ 0x00000080[64], "Scratch registers" {
///     31:0    value as u32;
/// });
///
/// // Read scratch register 0, i.e. I/O address `0x80`.
/// let scratch_0 = SCRATCH::read(bar, 0).value();
/// // Read scratch register 15, i.e. I/O address `0x80 + (15 * 4)`.
/// let scratch_15 = SCRATCH::read(bar, 15).value();
///
/// // This is out of bounds and won't build.
/// // let scratch_128 = SCRATCH::read(bar, 128).value();
///
/// // Runtime-obtained array index.
/// let scratch_idx = get_scratch_idx();
/// // Access on a runtime index returns an error if it is out-of-bounds.
/// let some_scratch = SCRATCH::try_read(bar, scratch_idx)?.value();
///
/// // Alias to a particular register in an array.
/// // Here `SCRATCH[8]` is used to convey the firmware exit code.
/// register!(FIRMWARE_STATUS => SCRATCH[8], "Firmware exit status code" {
///     7:0     status as u8;
/// });
///
/// let status = FIRMWARE_STATUS::read(bar).status();
///
/// // Non-contiguous register arrays can be defined by adding a stride parameter.
/// // Here, each of the 16 registers of the array are separated by 8 bytes, meaning that the
/// // registers of the two declarations below are interleaved.
/// register!(SCRATCH_INTERLEAVED_0 @ 0x000000c0[16 ; 8], "Scratch registers bank 0" {
///     31:0    value as u32;
/// });
/// register!(SCRATCH_INTERLEAVED_1 @ 0x000000c4[16 ; 8], "Scratch registers bank 1" {
///     31:0    value as u32;
/// });
/// # Ok(())
/// # }
/// ```
///
/// ## Relative arrays of registers
///
/// Combining the two features described in the sections above, arrays of registers accessible from
/// a base can also be defined:
///
/// ```no_run
/// # fn no_run() -> Result<(), Error> {
/// # fn get_scratch_idx() -> usize {
/// #   0x15
/// # }
/// // Type used as parameter of `RegisterBase` to specify the base.
/// pub(crate) struct CpuCtlBase;
///
/// // ZST describing `CPU0`.
/// struct Cpu0;
/// impl RegisterBase<CpuCtlBase> for Cpu0 {
///     const BASE: usize = 0x100;
/// }
/// // Singleton of `CPU0` used to identify it.
/// const CPU0: Cpu0 = Cpu0;
///
/// // ZST describing `CPU1`.
/// struct Cpu1;
/// impl RegisterBase<CpuCtlBase> for Cpu1 {
///     const BASE: usize = 0x200;
/// }
/// // Singleton of `CPU1` used to identify it.
/// const CPU1: Cpu1 = Cpu1;
///
/// // 64 per-cpu scratch registers, arranged as an contiguous array.
/// register!(CPU_SCRATCH @ CpuCtlBase[0x00000080[64]], "Per-CPU scratch registers" {
///     31:0    value as u32;
/// });
///
/// let cpu0_scratch_0 = CPU_SCRATCH::read(bar, &Cpu0, 0).value();
/// let cpu1_scratch_15 = CPU_SCRATCH::read(bar, &Cpu1, 15).value();
///
/// // This won't build.
/// // let cpu0_scratch_128 = CPU_SCRATCH::read(bar, &Cpu0, 128).value();
///
/// // Runtime-obtained array index.
/// let scratch_idx = get_scratch_idx();
/// // Access on a runtime value returns an error if it is out-of-bounds.
/// let cpu0_some_scratch = CPU_SCRATCH::try_read(bar, &Cpu0, scratch_idx)?.value();
///
/// // `SCRATCH[8]` is used to convey the firmware exit code.
/// register!(CPU_FIRMWARE_STATUS => CpuCtlBase[CPU_SCRATCH[8]],
///     "Per-CPU firmware exit status code" {
///     7:0     status as u8;
/// });
///
/// let cpu0_status = CPU_FIRMWARE_STATUS::read(bar, &Cpu0).status();
///
/// // Non-contiguous register arrays can be defined by adding a stride parameter.
/// // Here, each of the 16 registers of the array are separated by 8 bytes, meaning that the
/// // registers of the two declarations below are interleaved.
/// register!(CPU_SCRATCH_INTERLEAVED_0 @ CpuCtlBase[0x00000d00[16 ; 8]],
///           "Scratch registers bank 0" {
///     31:0    value as u32;
/// });
/// register!(CPU_SCRATCH_INTERLEAVED_1 @ CpuCtlBase[0x00000d04[16 ; 8]],
///           "Scratch registers bank 1" {
///     31:0    value as u32;
/// });
/// # Ok(())
/// # }
/// ```
macro_rules! register {
    // Creates a register at a fixed offset of the MMIO space.
    ($name:ident @ $offset:literal $(, $comment:literal)? { $($fields:tt)* } ) => {
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_fixed $name @ $offset);
    };

    // Creates an alias register of fixed offset register `alias` with its own fields.
    ($name:ident => $alias:ident $(, $comment:literal)? { $($fields:tt)* } ) => {
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_fixed $name @ $alias::OFFSET);
    };

    // Creates a register at a relative offset from a base address provider.
    ($name:ident @ $base:ty [ $offset:literal ] $(, $comment:literal)? { $($fields:tt)* } ) => {
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_relative $name @ $base [ $offset ]);
    };

    // Creates an alias register of relative offset register `alias` with its own fields.
    ($name:ident => $base:ty [ $alias:ident ] $(, $comment:literal)? { $($fields:tt)* }) => {
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_relative $name @ $base [ $alias::OFFSET ]);
    };

    // Creates an array of registers at a fixed offset of the MMIO space.
    (
        $name:ident @ $offset:literal [ $size:expr ; $stride:expr ] $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        static_assert!(::core::mem::size_of::<u32>() <= $stride);
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_array $name @ $offset [ $size ; $stride ]);
    };

    // Shortcut for contiguous array of registers (stride == size of element).
    (
        $name:ident @ $offset:literal [ $size:expr ] $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!($name @ $offset [ $size ; ::core::mem::size_of::<u32>() ] $(, $comment)? {
            $($fields)*
        } );
    };

    // Creates an array of registers at a relative offset from a base address provider.
    (
        $name:ident @ $base:ty [ $offset:literal [ $size:expr ; $stride:expr ] ]
            $(, $comment:literal)? { $($fields:tt)* }
    ) => {
        static_assert!(::core::mem::size_of::<u32>() <= $stride);
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_relative_array $name @ $base [ $offset [ $size ; $stride ] ]);
    };

    // Shortcut for contiguous array of relative registers (stride == size of element).
    (
        $name:ident @ $base:ty [ $offset:literal [ $size:expr ] ] $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!($name @ $base [ $offset [ $size ; ::core::mem::size_of::<u32>() ] ]
            $(, $comment)? { $($fields)* } );
    };

    // Creates an alias of register `idx` of relative array of registers `alias` with its own
    // fields.
    (
        $name:ident => $base:ty [ $alias:ident [ $idx:expr ] ] $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        static_assert!($idx < $alias::SIZE);
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_relative $name @ $base [ $alias::OFFSET + $idx * $alias::STRIDE ] );
    };

    // Creates an alias of register `idx` of array of registers `alias` with its own fields.
    // This rule belongs to the (non-relative) register arrays set, but needs to be put last
    // to avoid it being interpreted in place of the relative register array alias rule.
    ($name:ident => $alias:ident [ $idx:expr ] $(, $comment:literal)? { $($fields:tt)* }) => {
        static_assert!($idx < $alias::SIZE);
        bitfield!(pub(crate) struct $name(u32) $(, $comment)? { $($fields)* } );
        register!(@io_fixed $name @ $alias::OFFSET + $idx * $alias::STRIDE );
    };

    // Generates the IO accessors for a fixed offset register.
    (@io_fixed $name:ident @ $offset:expr) => {
        #[allow(dead_code)]
        impl $name {
            pub(crate) const OFFSET: usize = $offset;

            /// Read the register from its address in `io`.
            #[inline(always)]
            pub(crate) fn read<T, I>(io: &T) -> Self where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                Self(io.read32($offset))
            }

            /// Write the value contained in `self` to the register address in `io`.
            #[inline(always)]
            pub(crate) fn write<T, I>(self, io: &T) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                io.write32(self.0, $offset)
            }

            /// Read the register from its address in `io` and run `f` on its value to obtain a new
            /// value to write back.
            #[inline(always)]
            pub(crate) fn update<T, I, F>(
                io: &T,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io));
                reg.write(io);
            }
        }
    };

    // Generates the IO accessors for a relative offset register.
    (@io_relative $name:ident @ $base:ty [ $offset:expr ]) => {
        #[allow(dead_code)]
        impl $name {
            pub(crate) const OFFSET: usize = $offset;

            /// Read the register from `io`, using the base address provided by `base` and adding
            /// the register's offset to it.
            #[inline(always)]
            pub(crate) fn read<T, I, B>(
                io: &T,
                #[allow(unused_variables)]
                base: &B,
            ) -> Self where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                const OFFSET: usize = $name::OFFSET;

                let value = io.read32(
                    <B as crate::regs::macros::RegisterBase<$base>>::BASE + OFFSET
                );

                Self(value)
            }

            /// Write the value contained in `self` to `io`, using the base address provided by
            /// `base` and adding the register's offset to it.
            #[inline(always)]
            pub(crate) fn write<T, I, B>(
                self,
                io: &T,
                #[allow(unused_variables)]
                base: &B,
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                const OFFSET: usize = $name::OFFSET;

                io.write32(
                    self.0,
                    <B as crate::regs::macros::RegisterBase<$base>>::BASE + OFFSET
                );
            }

            /// Read the register from `io`, using the base address provided by `base` and adding
            /// the register's offset to it, then run `f` on its value to obtain a new value to
            /// write back.
            #[inline(always)]
            pub(crate) fn update<T, I, B, F>(
                io: &T,
                base: &B,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io, base));
                reg.write(io, base);
            }
        }
    };

    // Generates the IO accessors for an array of registers.
    (@io_array $name:ident @ $offset:literal [ $size:expr ; $stride:expr ]) => {
        #[allow(dead_code)]
        impl $name {
            pub(crate) const OFFSET: usize = $offset;
            pub(crate) const SIZE: usize = $size;
            pub(crate) const STRIDE: usize = $stride;

            /// Read the array register at index `idx` from its address in `io`.
            #[inline(always)]
            pub(crate) fn read<T, I>(
                io: &T,
                idx: usize,
            ) -> Self where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                build_assert!(idx < Self::SIZE);

                let offset = Self::OFFSET + (idx * Self::STRIDE);
                let value = io.read32(offset);

                Self(value)
            }

            /// Write the value contained in `self` to the array register with index `idx` in `io`.
            #[inline(always)]
            pub(crate) fn write<T, I>(
                self,
                io: &T,
                idx: usize
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                build_assert!(idx < Self::SIZE);

                let offset = Self::OFFSET + (idx * Self::STRIDE);

                io.write32(self.0, offset);
            }

            /// Read the array register at index `idx` in `io` and run `f` on its value to obtain a
            /// new value to write back.
            #[inline(always)]
            pub(crate) fn update<T, I, F>(
                io: &T,
                idx: usize,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io, idx));
                reg.write(io, idx);
            }

            /// Read the array register at index `idx` from its address in `io`.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_read<T, I>(
                io: &T,
                idx: usize,
            ) -> ::kernel::error::Result<Self> where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                if idx < Self::SIZE {
                    Ok(Self::read(io, idx))
                } else {
                    Err(EINVAL)
                }
            }

            /// Write the value contained in `self` to the array register with index `idx` in `io`.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_write<T, I>(
                self,
                io: &T,
                idx: usize,
            ) -> ::kernel::error::Result where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
            {
                if idx < Self::SIZE {
                    Ok(self.write(io, idx))
                } else {
                    Err(EINVAL)
                }
            }

            /// Read the array register at index `idx` in `io` and run `f` on its value to obtain a
            /// new value to write back.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_update<T, I, F>(
                io: &T,
                idx: usize,
                f: F,
            ) -> ::kernel::error::Result where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                if idx < Self::SIZE {
                    Ok(Self::update(io, idx, f))
                } else {
                    Err(EINVAL)
                }
            }
        }
    };

    // Generates the IO accessors for an array of relative registers.
    (
        @io_relative_array $name:ident @ $base:ty
            [ $offset:literal [ $size:expr ; $stride:expr ] ]
    ) => {
        #[allow(dead_code)]
        impl $name {
            pub(crate) const OFFSET: usize = $offset;
            pub(crate) const SIZE: usize = $size;
            pub(crate) const STRIDE: usize = $stride;

            /// Read the array register at index `idx` from `io`, using the base address provided
            /// by `base` and adding the register's offset to it.
            #[inline(always)]
            pub(crate) fn read<T, I, B>(
                io: &T,
                #[allow(unused_variables)]
                base: &B,
                idx: usize,
            ) -> Self where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                build_assert!(idx < Self::SIZE);

                let offset = <B as crate::regs::macros::RegisterBase<$base>>::BASE +
                    Self::OFFSET + (idx * Self::STRIDE);
                let value = io.read32(offset);

                Self(value)
            }

            /// Write the value contained in `self` to `io`, using the base address provided by
            /// `base` and adding the offset of array register `idx` to it.
            #[inline(always)]
            pub(crate) fn write<T, I, B>(
                self,
                io: &T,
                #[allow(unused_variables)]
                base: &B,
                idx: usize
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                build_assert!(idx < Self::SIZE);

                let offset = <B as crate::regs::macros::RegisterBase<$base>>::BASE +
                    Self::OFFSET + (idx * Self::STRIDE);

                io.write32(self.0, offset);
            }

            /// Read the array register at index `idx` from `io`, using the base address provided
            /// by `base` and adding the register's offset to it, then run `f` on its value to
            /// obtain a new value to write back.
            #[inline(always)]
            pub(crate) fn update<T, I, B, F>(
                io: &T,
                base: &B,
                idx: usize,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io, base, idx));
                reg.write(io, base, idx);
            }

            /// Read the array register at index `idx` from `io`, using the base address provided
            /// by `base` and adding the register's offset to it.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_read<T, I, B>(
                io: &T,
                base: &B,
                idx: usize,
            ) -> ::kernel::error::Result<Self> where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                if idx < Self::SIZE {
                    Ok(Self::read(io, base, idx))
                } else {
                    Err(EINVAL)
                }
            }

            /// Write the value contained in `self` to `io`, using the base address provided by
            /// `base` and adding the offset of array register `idx` to it.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_write<T, I, B>(
                self,
                io: &T,
                base: &B,
                idx: usize,
            ) -> ::kernel::error::Result where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
            {
                if idx < Self::SIZE {
                    Ok(self.write(io, base, idx))
                } else {
                    Err(EINVAL)
                }
            }

            /// Read the array register at index `idx` from `io`, using the base address provided
            /// by `base` and adding the register's offset to it, then run `f` on its value to
            /// obtain a new value to write back.
            ///
            /// The validity of `idx` is checked at run-time, and `EINVAL` is returned is the
            /// access was out-of-bounds.
            #[inline(always)]
            pub(crate) fn try_update<T, I, B, F>(
                io: &T,
                base: &B,
                idx: usize,
                f: F,
            ) -> ::kernel::error::Result where
                T: ::core::ops::Deref<Target = I>,
                I: ::kernel::io::IoKnownSize + ::kernel::io::IoCapable<u32>,
                B: crate::regs::macros::RegisterBase<$base>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                if idx < Self::SIZE {
                    Ok(Self::update(io, base, idx, f))
                } else {
                    Err(EINVAL)
                }
            }
        }
    };
}
