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
///     pub struct ControlReg(u32) {
///         7:7 state as bool => State;
///         3:0 mode as u8 ?=> Mode;
///     }
/// }
/// ```
///
/// This generates a struct with:
/// - Field accessors: `mode()`, `state()`, etc.
/// - Field setters: `set_mode()`, `set_state()`, etc. (supports chaining with builder pattern).
///   Note that the compiler will error out if the size of the setter's arg exceeds the
///   struct's storage size.
/// - Debug and Default implementations.
///
/// Note: Field accessors and setters inherit the same visibility as the struct itself.
/// In the example above, both `mode()` and `set_mode()` methods will be `pub`.
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
    ($vis:vis struct $name:ident($storage:ty) $(, $comment:literal)? { $($fields:tt)* }) => {
        bitfield!(@core $vis $name $storage $(, $comment)? { $($fields)* });
    };

    // All rules below are helpers.

    // Defines the wrapper `$name` type, as well as its relevant implementations (`Debug`,
    // `Default`, and conversion to the value type) and field accessor methods.
    (@core $vis:vis $name:ident $storage:ty $(, $comment:literal)? { $($fields:tt)* }) => {
        $(
        #[doc=$comment]
        )?
        #[repr(transparent)]
        #[derive(Clone, Copy)]
        $vis struct $name($storage);

        impl ::core::convert::From<$name> for $storage {
            fn from(val: $name) -> $storage {
                val.0
            }
        }

        bitfield!(@fields_dispatcher $vis $name $storage { $($fields)* });
    };

    // Captures the fields and passes them to all the implementers that require field information.
    //
    // Used to simplify the matching rules for implementers, so they don't need to match the entire
    // complex fields rule even though they only make use of part of it.
    (@fields_dispatcher $vis:vis $name:ident $storage:ty {
        $($hi:tt:$lo:tt $field:ident as $type:tt
            $(?=> $try_into_type:ty)?
            $(=> $into_type:ty)?
            $(, $comment:literal)?
        ;
        )*
    }
    ) => {
        bitfield!(@field_accessors $vis $name $storage {
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
        @field_accessors $vis:vis $name:ident $storage:ty {
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
            bitfield!(@field_accessor $vis $name $storage, $hi:$lo $field as $type
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
        @field_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident as bool
            => $into_type:ty $(, $comment:literal)?;
    ) => {
        bitfield!(
            @leaf_accessor $vis $name $storage, $hi:$lo $field
            { |f| <$into_type>::from(f != 0) }
            bool $into_type => $into_type $(, $comment)?;
        );
    };

    // Shortcut for fields defined as `bool` without the `=>` syntax.
    (
        @field_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident as bool
            $(, $comment:literal)?;
    ) => {
        bitfield!(
            @field_accessor $vis $name $storage, $hi:$lo $field as bool => bool $(, $comment)?;
        );
    };

    // Catches the `?=>` syntax for non-boolean fields.
    (
        @field_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident as $type:tt
            ?=> $try_into_type:ty $(, $comment:literal)?;
    ) => {
        bitfield!(@leaf_accessor $vis $name $storage, $hi:$lo $field
            { |f| <$try_into_type>::try_from(f as $type) } $type $try_into_type =>
            ::core::result::Result<
                $try_into_type,
                <$try_into_type as ::core::convert::TryFrom<$type>>::Error
            >
            $(, $comment)?;);
    };

    // Catches the `=>` syntax for non-boolean fields.
    (
        @field_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident as $type:tt
            => $into_type:ty $(, $comment:literal)?;
    ) => {
        bitfield!(@leaf_accessor $vis $name $storage, $hi:$lo $field
            { |f| <$into_type>::from(f as $type) } $type $into_type => $into_type $(, $comment)?;);
    };

    // Shortcut for non-boolean fields defined without the `=>` or `?=>` syntax.
    (
        @field_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident as $type:tt
            $(, $comment:literal)?;
    ) => {
        bitfield!(
            @field_accessor $vis $name $storage, $hi:$lo $field as $type => $type $(, $comment)?;
        );
    };

    // Generates the accessor methods for a single field.
    (
        @leaf_accessor $vis:vis $name:ident $storage:ty, $hi:tt:$lo:tt $field:ident
            { $process:expr } $prim_type:tt $to_type:ty => $res_type:ty $(, $comment:literal)?;
    ) => {
        ::kernel::macros::paste!(
        const [<$field:upper _RANGE>]: ::core::ops::RangeInclusive<u8> = $lo..=$hi;
        const [<$field:upper _MASK>]: $storage = {
            // Generate mask for shifting
            match ::core::mem::size_of::<$storage>() {
                1 => ::kernel::bits::genmask_u8($lo..=$hi) as $storage,
                2 => ::kernel::bits::genmask_u16($lo..=$hi) as $storage,
                4 => ::kernel::bits::genmask_u32($lo..=$hi) as $storage,
                8 => ::kernel::bits::genmask_u64($lo..=$hi) as $storage,
                _ => ::kernel::build_error!("Unsupported storage type size")
            }
        };
        const [<$field:upper _SHIFT>]: u32 = $lo;
        );

        $(
        #[doc="Returns the value of this field:"]
        #[doc=$comment]
        )?
        #[inline(always)]
        $vis fn $field(self) -> $res_type {
            ::kernel::macros::paste!(
            const MASK: $storage = $name::[<$field:upper _MASK>];
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
        $vis fn [<set_ $field>](mut self, value: $to_type) -> Self {
            const MASK: $storage = $name::[<$field:upper _MASK>];
            const SHIFT: u32 = $name::[<$field:upper _SHIFT>];
            let value = ($storage::from($prim_type::from(value)) << SHIFT) & MASK;
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
