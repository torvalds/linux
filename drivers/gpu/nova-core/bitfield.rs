// SPDX-License-Identifier: GPL-2.0

//! Bitfield library for Rust structures
//!
//! Support for defining bitfields in Rust structures. Also used by the [`register!`] macro.

/// Defines a struct with accessors to access bits within an inner unsigned integer.
///
/// # Syntax
///
/// ```rust
/// use nova_core::bitfield;
///
/// #[derive(Debug, Clone, Copy, Default)]
/// enum Mode {
///     #[default]
///     Low = 0,
///     High = 1,
///     Auto = 2,
/// }
///
/// impl TryFrom<u8> for Mode {
///     type Error = u8;
///     fn try_from(value: u8) -> Result<Self, Self::Error> {
///         match value {
///             0 => Ok(Mode::Low),
///             1 => Ok(Mode::High),
///             2 => Ok(Mode::Auto),
///             _ => Err(value),
///         }
///     }
/// }
///
/// impl From<Mode> for u8 {
///     fn from(mode: Mode) -> u8 {
///         mode as u8
///     }
/// }
///
/// #[derive(Debug, Clone, Copy, Default)]
/// enum State {
///     #[default]
///     Inactive = 0,
///     Active = 1,
/// }
///
/// impl From<bool> for State {
///     fn from(value: bool) -> Self {
///         if value { State::Active } else { State::Inactive }
///     }
/// }
///
/// impl From<State> for bool {
///     fn from(state: State) -> bool {
///         match state {
///             State::Inactive => false,
///             State::Active => true,
///         }
///     }
/// }
///
/// bitfield! {
///     struct ControlReg {
///         7:7 state as bool => State;
///         3:0 mode as u8 ?=> Mode;
///     }
/// }
/// ```
///
/// This generates a struct with:
/// - Field accessors: `mode()`, `state()`, etc.
/// - Field setters: `set_mode()`, `set_state()`, etc. (supports chaining with builder pattern).
/// - Debug and Default implementations.
///
/// Fields are defined as follows:
///
/// - `as <type>` simply returns the field value casted to <type>, typically `u32`, `u16`, `u8` or
///   `bool`. Note that `bool` fields must have a range of 1 bit.
/// - `as <type> => <into_type>` calls `<into_type>`'s `From::<<type>>` implementation and returns
///   the result.
/// - `as <type> ?=> <try_into_type>` calls `<try_into_type>`'s `TryFrom::<<type>>` implementation
///   and returns the result. This is useful with fields for which not all values are valid.
macro_rules! bitfield {
    // Main entry point - defines the bitfield struct with fields
    (struct $name:ident $(, $comment:literal)? { $($fields:tt)* }) => {
        bitfield!(@core $name $(, $comment)? { $($fields)* });
    };

    // All rules below are helpers.

    // Defines the wrapper `$name` type, as well as its relevant implementations (`Debug`,
    // `Default`, `BitOr`, and conversion to the value type) and field accessor methods.
    (@core $name:ident $(, $comment:literal)? { $($fields:tt)* }) => {
        $(
        #[doc=$comment]
        )?
        #[repr(transparent)]
        #[derive(Clone, Copy)]
        pub(crate) struct $name(u32);

        impl ::core::ops::BitOr for $name {
            type Output = Self;

            fn bitor(self, rhs: Self) -> Self::Output {
                Self(self.0 | rhs.0)
            }
        }

        impl ::core::convert::From<$name> for u32 {
            fn from(val: $name) -> u32 {
                val.0
            }
        }

        bitfield!(@fields_dispatcher $name { $($fields)* });
    };

    // Captures the fields and passes them to all the implementers that require field information.
    //
    // Used to simplify the matching rules for implementers, so they don't need to match the entire
    // complex fields rule even though they only make use of part of it.
    (@fields_dispatcher $name:ident {
        $($hi:tt:$lo:tt $field:ident as $type:tt
            $(?=> $try_into_type:ty)?
            $(=> $into_type:ty)?
            $(, $comment:literal)?
        ;
        )*
    }
    ) => {
        bitfield!(@field_accessors $name {
            $(
                $hi:$lo $field as $type
                $(?=> $try_into_type)?
                $(=> $into_type)?
                $(, $comment)?
            ;
            )*
        });
        bitfield!(@debug $name { $($field;)* });
        bitfield!(@default $name { $($field;)* });
    };

    // Defines all the field getter/setter methods for `$name`.
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
            bitfield!(@check_field_bounds $hi:$lo $field as $type);
        )*

        #[allow(dead_code)]
        impl $name {
            $(
            bitfield!(@field_accessor $name $hi:$lo $field as $type
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
        bitfield!(
            @leaf_accessor $name $hi:$lo $field
            { |f| <$into_type>::from(if f != 0 { true } else { false }) }
            bool $into_type => $into_type $(, $comment)?;
        );
    };

    // Shortcut for fields defined as `bool` without the `=>` syntax.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as bool $(, $comment:literal)?;
    ) => {
        bitfield!(@field_accessor $name $hi:$lo $field as bool => bool $(, $comment)?;);
    };

    // Catches the `?=>` syntax for non-boolean fields.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:tt ?=> $try_into_type:ty
            $(, $comment:literal)?;
    ) => {
        bitfield!(@leaf_accessor $name $hi:$lo $field
            { |f| <$try_into_type>::try_from(f as $type) } $type $try_into_type =>
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
        bitfield!(@leaf_accessor $name $hi:$lo $field
            { |f| <$into_type>::from(f as $type) } $type $into_type => $into_type $(, $comment)?;);
    };

    // Shortcut for non-boolean fields defined without the `=>` or `?=>` syntax.
    (
        @field_accessor $name:ident $hi:tt:$lo:tt $field:ident as $type:tt
            $(, $comment:literal)?;
    ) => {
        bitfield!(@field_accessor $name $hi:$lo $field as $type => $type $(, $comment)?;);
    };

    // Generates the accessor methods for a single field.
    (
        @leaf_accessor $name:ident $hi:tt:$lo:tt $field:ident
            { $process:expr } $prim_type:tt $to_type:ty => $res_type:ty $(, $comment:literal)?;
    ) => {
        ::kernel::macros::paste!(
        const [<$field:upper _RANGE>]: ::core::ops::RangeInclusive<u8> = $lo..=$hi;
        const [<$field:upper _MASK>]: u32 = ((((1 << $hi) - 1) << 1) + 1) - ((1 << $lo) - 1);
        const [<$field:upper _SHIFT>]: u32 = Self::[<$field:upper _MASK>].trailing_zeros();
        );

        $(
        #[doc="Returns the value of this field:"]
        #[doc=$comment]
        )?
        #[inline(always)]
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
        #[inline(always)]
        pub(crate) fn [<set_ $field>](mut self, value: $to_type) -> Self {
            const MASK: u32 = $name::[<$field:upper _MASK>];
            const SHIFT: u32 = $name::[<$field:upper _SHIFT>];
            let value = (u32::from($prim_type::from(value)) << SHIFT) & MASK;
            self.0 = (self.0 & !MASK) | value;

            self
        }
        );
    };

    // Generates the `Debug` implementation for `$name`.
    (@debug $name:ident { $($field:ident;)* }) => {
        impl ::kernel::fmt::Debug for $name {
            fn fmt(&self, f: &mut ::kernel::fmt::Formatter<'_>) -> ::kernel::fmt::Result {
                f.debug_struct(stringify!($name))
                    .field("<raw>", &::kernel::prelude::fmt!("{:#x}", &self.0))
                $(
                    .field(stringify!($field), &self.$field())
                )*
                    .finish()
            }
        }
    };

    // Generates the `Default` implementation for `$name`.
    (@default $name:ident { $($field:ident;)* }) => {
        /// Returns a value for the bitfield where all fields are set to their default value.
        impl ::core::default::Default for $name {
            fn default() -> Self {
                #[allow(unused_mut)]
                let mut value = Self(Default::default());

                ::kernel::macros::paste!(
                $(
                value.[<set_ $field>](Default::default());
                )*
                );

                value
            }
        }
    };
}
