// SPDX-License-Identifier: GPL-2.0

//! Macro to define register layout and accessors.
//!
//! A single register typically includes several fields, which are accessed through a combination
//! of bit-shift and mask operations that introduce a class of potential mistakes, notably because
//! not all possible field values are necessarily valid.
//!
//! The macro in this module allow to define, using an intruitive and readable syntax, a dedicated
//! type for each register with its own field accessors that can return an error is a field's value
//! is invalid.

/// Defines a dedicated type for a register with an absolute offset, alongside with getter and
/// setter methods for its fields and methods to read and write it from an `Io` region.
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
/// region. It is composed of 3 fields, for instance `minor_revision` is made of the 4 less
/// significant bits of the register. Each field can be accessed and modified using accessor
/// methods:
///
/// ```no_run
/// // Read from the register's defined offset (0x100).
/// let boot0 = BOOT_0::read(&bar);
/// pr_info!("chip revision: {}.{}", boot0.major_revision(), boot0.minor_revision());
///
/// // `Chipset::try_from` will be called with the value of the field and returns an error if the
/// // value is invalid.
/// let chipset = boot0.chipset()?;
///
/// // Update some fields and write the value back.
/// boot0.set_major_revision(3).set_minor_revision(10).write(&bar);
///
/// // Or just read and update the register in a single step:
/// BOOT_0::alter(&bar, |r| r.set_major_revision(3).set_minor_revision(10));
/// ```
///
/// Fields can be defined as follows:
///
/// - `as <type>` simply returns the field value casted as the requested integer type, typically
///   `u32`, `u16`, `u8` or `bool`. Note that `bool` fields must have a range of 1 bit.
/// - `as <type> => <into_type>` calls `<into_type>`'s `From::<<type>>` implementation and returns
///   the result.
/// - `as <type> ?=> <try_into_type>` calls `<try_into_type>`'s `TryFrom::<<type>>` implementation
///   and returns the result. This is useful on fields for which not all values are value.
///
/// The documentation strings are optional. If present, they will be added to the type's
/// definition, or the field getter and setter methods they are attached to.
///
/// Putting a `+` before the address of the register makes it relative to a base: the `read` and
/// `write` methods take a `base` argument that is added to the specified address before access,
/// and `try_read` and `try_write` methods are also created, allowing access with offsets unknown
/// at compile-time:
///
/// ```no_run
/// register!(CPU_CTL @ +0x0000010, "CPU core control" {
///    0:0     start as bool, "Start the CPU core";
/// });
///
/// // Flip the `start` switch for the CPU core which base address is at `CPU_BASE`.
/// let cpuctl = CPU_CTL::read(&bar, CPU_BASE);
/// pr_info!("CPU CTL: {:#x}", cpuctl);
/// cpuctl.set_start(true).write(&bar, CPU_BASE);
/// ```
///
/// It is also possible to create a alias register by using the `=> ALIAS` syntax. This is useful
/// for cases where a register's interpretation depends on the context:
///
/// ```no_run
/// register!(SCRATCH_0 @ 0x0000100, "Scratch register 0" {
///    31:0     value as u32, "Raw value";
///
/// register!(SCRATCH_0_BOOT_STATUS => SCRATCH_0, "Boot status of the firmware" {
///     0:0     completed as bool, "Whether the firmware has completed booting";
/// ```
///
/// In this example, `SCRATCH_0_BOOT_STATUS` uses the same I/O address as `SCRATCH_0`, while also
/// providing its own `completed` method.
macro_rules! register {
    // Creates a register at a fixed offset of the MMIO space.
    (
        $name:ident @ $offset:literal $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!(@common $name @ $offset $(, $comment)?);
        register!(@field_accessors $name { $($fields)* });
        register!(@io $name @ $offset);
    };

    // Creates a alias register of fixed offset register `alias` with its own fields.
    (
        $name:ident => $alias:ident $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!(@common $name @ $alias::OFFSET $(, $comment)?);
        register!(@field_accessors $name { $($fields)* });
        register!(@io $name @ $alias::OFFSET);
    };

    // Creates a register at a relative offset from a base address.
    (
        $name:ident @ + $offset:literal $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!(@common $name @ $offset $(, $comment)?);
        register!(@field_accessors $name { $($fields)* });
        register!(@io$name @ + $offset);
    };

    // Creates a alias register of relative offset register `alias` with its own fields.
    (
        $name:ident => + $alias:ident $(, $comment:literal)? {
            $($fields:tt)*
        }
    ) => {
        register!(@common $name @ $alias::OFFSET $(, $comment)?);
        register!(@field_accessors $name { $($fields)* });
        register!(@io $name @ + $alias::OFFSET);
    };

    // All rules below are helpers.

    // Defines the wrapper `$name` type, as well as its relevant implementations (`Debug`, `BitOr`,
    // and conversion to regular `u32`).
    (@common $name:ident @ $offset:expr $(, $comment:literal)?) => {
        $(
        #[doc=$comment]
        )?
        #[repr(transparent)]
        #[derive(Clone, Copy, Default)]
        pub(crate) struct $name(u32);

        #[allow(dead_code)]
        impl $name {
            pub(crate) const OFFSET: usize = $offset;
        }

        // TODO[REGA]: display the raw hex value, then the value of all the fields. This requires
        // matching the fields, which will complexify the syntax considerably...
        impl ::core::fmt::Debug for $name {
            fn fmt(&self, f: &mut ::core::fmt::Formatter<'_>) -> ::core::fmt::Result {
                f.debug_tuple(stringify!($name))
                    .field(&format_args!("0x{0:x}", &self.0))
                    .finish()
            }
        }

        impl ::core::ops::BitOr for $name {
            type Output = Self;

            fn bitor(self, rhs: Self) -> Self::Output {
                Self(self.0 | rhs.0)
            }
        }

        impl ::core::convert::From<$name> for u32 {
            fn from(reg: $name) -> u32 {
                reg.0
            }
        }
    };

    // Defines all the field getter/methods methods for `$name`.
    (
        @field_accessors $name:ident {
        $($hi:tt:$lo:tt $field:ident as $type:tt
            $(?=> $try_into_type:ty)?
            $(=> $into_type:ty)?
            $(, $comment:literal)?
        ;
        )*
        }
    ) => {
        $(
            register!(@check_field_bounds $hi:$lo $field as $type);
        )*

        #[allow(dead_code)]
        impl $name {
            $(
            register!(@field_accessor $name $hi:$lo $field as $type
                $(?=> $try_into_type)?
                $(=> $into_type)?
                $(, $comment)?
                ;
            );
            )*
        }
    };

    // Boolean fields must have `$hi == $lo`.
    (@check_field_bounds $hi:tt:$lo:tt $field:ident as bool) => {
        #[allow(clippy::eq_op)]
        const _: () = {
            ::kernel::build_assert!(
                $hi == $lo,
                concat!("boolean field `", stringify!($field), "` covers more than one bit")
            );
        };
    };

    // Non-boolean fields must have `$hi >= $lo`.
    (@check_field_bounds $hi:tt:$lo:tt $field:ident as $type:tt) => {
        #[allow(clippy::eq_op)]
        const _: () = {
            ::kernel::build_assert!(
                $hi >= $lo,
                concat!("field `", stringify!($field), "`'s MSB is smaller than its LSB")
            );
        };
    };

    // Catches fields defined as `bool` and convert them into a boolean value.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as bool => $into_type:ty
            $(, $comment:literal)?;
    ) => {
        register!(
            @leaf_accessor $name $hi:$lo $field as bool
            { |f| <$into_type>::from(if f != 0 { true } else { false }) }
            $into_type => $into_type $(, $comment)?;
        );
    };

    // Shortcut for fields defined as `bool` without the `=>` syntax.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as bool $(, $comment:literal)?;
    ) => {
        register!(@field_accessor $name $hi:$lo $field as bool => bool $(, $comment)?;);
    };

    // Catches the `?=>` syntax for non-boolean fields.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:tt ?=> $try_into_type:ty
            $(, $comment:literal)?;
    ) => {
        register!(@leaf_accessor $name $hi:$lo $field as $type
            { |f| <$try_into_type>::try_from(f as $type) } $try_into_type =>
            ::core::result::Result<
                $try_into_type,
                <$try_into_type as ::core::convert::TryFrom<$type>>::Error
            >
            $(, $comment)?;);
    };

    // Catches the `=>` syntax for non-boolean fields.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:tt => $into_type:ty
            $(, $comment:literal)?;
    ) => {
        register!(@leaf_accessor $name $hi:$lo $field as $type
            { |f| <$into_type>::from(f as $type) } $into_type => $into_type $(, $comment)?;);
    };

    // Shortcut for fields defined as non-`bool` without the `=>` or `?=>` syntax.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:tt
            $(, $comment:literal)?;
    ) => {
        register!(@field_accessor $name $hi:$lo $field as $type => $type $(, $comment)?;);
    };

    // Generates the accessor methods for a single field.
    (
        @leaf_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:ty
            { $process:expr } $to_type:ty => $res_type:ty $(, $comment:literal)?;
    ) => {
        ::kernel::macros::paste!(
        const [<$field:upper>]: ::core::ops::RangeInclusive<u8> = $lo..=$hi;
        const [<$field:upper _MASK>]: u32 = ((((1 << $hi) - 1) << 1) + 1) - ((1 << $lo) - 1);
        const [<$field:upper _SHIFT>]: u32 = Self::[<$field:upper _MASK>].trailing_zeros();
        );

        $(
        #[doc="Returns the value of this field:"]
        #[doc=$comment]
        )?
        #[inline]
        pub(crate) fn $field(self) -> $res_type {
            ::kernel::macros::paste!(
            const MASK: u32 = $name::[<$field:upper _MASK>];
            const SHIFT: u32 = $name::[<$field:upper _SHIFT>];
            );
            let field = ((self.0 & MASK) >> SHIFT);

            $process(field)
        }

        ::kernel::macros::paste!(
        $(
        #[doc="Sets the value of this field:"]
        #[doc=$comment]
        )?
        #[inline]
        pub(crate) fn [<set_ $field>](mut self, value: $to_type) -> Self {
            const MASK: u32 = $name::[<$field:upper _MASK>];
            const SHIFT: u32 = $name::[<$field:upper _SHIFT>];
            let value = (u32::from(value) << SHIFT) & MASK;
            self.0 = (self.0 & !MASK) | value;

            self
        }
        );
    };

    // Creates the IO accessors for a fixed offset register.
    (@io $name:ident @ $offset:expr) => {
        #[allow(dead_code)]
        impl $name {
            #[inline]
            pub(crate) fn read<const SIZE: usize, T>(io: &T) -> Self where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                Self(io.read32($offset))
            }

            #[inline]
            pub(crate) fn write<const SIZE: usize, T>(self, io: &T) where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                io.write32(self.0, $offset)
            }

            #[inline]
            pub(crate) fn alter<const SIZE: usize, T, F>(
                io: &T,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io));
                reg.write(io);
            }
        }
    };

    // Create the IO accessors for a relative offset register.
    (@io $name:ident @ + $offset:literal) => {
        #[allow(dead_code)]
        impl $name {
            #[inline]
            pub(crate) fn read<const SIZE: usize, T>(
                io: &T,
                base: usize,
            ) -> Self where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                Self(io.read32(base + $offset))
            }

            #[inline]
            pub(crate) fn write<const SIZE: usize, T>(
                self,
                io: &T,
                base: usize,
            ) where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                io.write32(self.0, base + $offset)
            }

            #[inline]
            pub(crate) fn alter<const SIZE: usize, T, F>(
                io: &T,
                base: usize,
                f: F,
            ) where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::read(io, base));
                reg.write(io, base);
            }

            #[inline]
            pub(crate) fn try_read<const SIZE: usize, T>(
                io: &T,
                base: usize,
            ) -> ::kernel::error::Result<Self> where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                io.try_read32(base + $offset).map(Self)
            }

            #[inline]
            pub(crate) fn try_write<const SIZE: usize, T>(
                self,
                io: &T,
                base: usize,
            ) -> ::kernel::error::Result<()> where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
            {
                io.try_write32(self.0, base + $offset)
            }

            #[inline]
            pub(crate) fn try_alter<const SIZE: usize, T, F>(
                io: &T,
                base: usize,
                f: F,
            ) -> ::kernel::error::Result<()> where
                T: ::core::ops::Deref<Target = ::kernel::io::Io<SIZE>>,
                F: ::core::ops::FnOnce(Self) -> Self,
            {
                let reg = f(Self::try_read(io, base)?);
                reg.try_write(io, base)
            }
        }
    };
}
