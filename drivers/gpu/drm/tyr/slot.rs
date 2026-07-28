// SPDX-License-Identifier: GPL-2.0 or MIT

//! Slot management abstraction for limited hardware resources.
//!
//! This module provides a generic [`SlotManager`] that assigns limited hardware
//! slots to logical "seats". A seat represents an entity (such as a virtual memory
//! (VM) address space) that needs access to a hardware slot.
//!
//! The [`SlotManager`] tracks slot allocation using sequence numbers (seqno) to detect
//! when a seat's binding has been invalidated. When a seat requests activation,
//! the manager will either reuse the seat's existing slot (if still valid),
//! allocate a free slot (if any are available), or evict the oldest idle slot if any
//! slots are idle.
//!
//! Hardware-specific behavior is customized by implementing the [`SlotOperations`]
//! trait, which allows callbacks when slots are activated or evicted.
//!
//! This is currently used for managing address space slots in the GPU, and it will
//! also be used to manage Command Stream Group (CSG) interface slots in the future.
//!
//! [SlotOperations]: crate::slot::SlotOperations
//! [SlotManager]: crate::slot::SlotManager

use core::{
    mem,
    ops::{
        Deref,
        DerefMut, //
    }, //
};

use kernel::{
    prelude::*,
    sync::LockedBy, //
};

/// Seat information.
///
/// This can't be accessed directly by the element embedding a `Seat`,
/// but is used by the generic slot manager logic to control residency
/// of a certain object on a hardware slot.
pub(crate) struct SeatInfo {
    /// Slot used by this seat.
    ///
    /// This index is only valid if the slot pointed to by this index
    /// has its `SlotInfo::seqno` match `SeatInfo::seqno`. Otherwise,
    /// it means the object has been evicted from the hardware slot,
    /// and a new slot needs to be acquired to make this object
    /// resident again.
    slot: u8,

    /// Sequence number encoding the last time this seat was active.
    /// We also use it to check if a slot is still bound to a seat.
    seqno: u64,
}

/// Seat state.
///
/// This is meant to be embedded in the object that wants to acquire
/// hardware slots. It also starts in the `Seat::NoSeat` state, and
/// the slot manager will change the object value when an active/evict
/// request is issued.
#[derive(Default)]
pub(crate) enum Seat {
    #[expect(clippy::enum_variant_names)]
    /// Resource is not resident.
    ///
    /// All objects start with a seat in the `Seat::NoSeat` state. The seat also
    /// gets back to that state if the user requests eviction. It
    /// can also end up in that state next time an operation is done
    /// on a `Seat::Idle` seat and the slot manager finds out this
    /// object has been evicted from the slot.
    #[default]
    NoSeat,

    /// Resource is actively used and resident.
    ///
    /// When a seat is in the `Seat::Active` state, it can't be evicted, and the
    /// slot pointed to by `SeatInfo::slot` is guaranteed to be reserved
    /// for this object as long as the seat stays active.
    Active(SeatInfo),

    /// Resource is idle and might or might not be resident.
    ///
    /// When a seat is in the`Seat::Idle` state, we can't know for sure if the
    /// object is resident or evicted until the next request we issue
    /// to the slot manager. This tells the slot manager it can
    /// reclaim the underlying slot if needed.
    /// In order for the hardware to use this object again, the seat
    /// needs to be turned into an `Seat::Active` state again
    /// with a `SlotManager::activate()` call.
    Idle(SeatInfo),
}

impl Seat {
    /// Get the slot index this seat is pointing to.
    ///
    /// If the seat is not `Seat::Active` we can't trust the
    /// `SeatInfo`. In that case `None` is returned, otherwise
    /// `Some(SeatInfo::slot)` is returned.
    pub(crate) fn slot(&self) -> Option<u8> {
        match self {
            Self::Active(info) => Some(info.slot),
            _ => None,
        }
    }
}

/// Information related to a slot.
struct SlotInfo<D> {
    /// Type specific data attached to a slot.
    slot_data: D,

    /// Sequence number from when this slot was last activated.
    seqno: u64,
}

/// Slot state.
#[derive(Default)]
enum Slot<D> {
    /// Slot is free.
    #[default]
    Free,

    /// Slot is active.
    Active(SlotInfo<D>),

    /// Slot is idle.
    Idle(SlotInfo<D>),
}

pub(crate) type LockedSeat<T, const MAX_SLOTS: usize> = LockedBy<Seat, SlotManager<T, MAX_SLOTS>>;

/// Trait describing the slot-related operations.
pub(crate) trait SlotOperations<const MAX_SLOTS: usize>: Sized {
    /// Implementation-specific data associated with each slot.
    type SlotData;

    /// Returns the seat belonging to this slot data.
    fn seat(slot_data: &Self::SlotData) -> &LockedSeat<Self, MAX_SLOTS>;

    /// Called when a slot is being activated for a seat.
    fn activate(&mut self, _slot_idx: usize, _slot_data: &Self::SlotData) -> Result {
        Ok(())
    }

    /// Called when a slot is being evicted and freed.
    fn evict(&mut self, _slot_idx: usize, _slot_data: &Self::SlotData) -> Result {
        Ok(())
    }
}

/// A generic slot manager that provides access to a limited number of hardware slots.
pub(crate) struct SlotManager<T: SlotOperations<MAX_SLOTS>, const MAX_SLOTS: usize> {
    /// A specific implementation of the generic slot manager.
    manager: T,

    /// Number of slots actually available.
    slot_count: usize,

    /// Slot array used to track the state of each slot.
    slots: [Slot<T::SlotData>; MAX_SLOTS],

    /// Sequence number incremented each time a Seat is successfully activated
    use_seqno: u64,
}

impl<T: SlotOperations<MAX_SLOTS>, const MAX_SLOTS: usize> SlotManager<T, MAX_SLOTS> {
    /// Creates a specific instance of a slot manager.
    pub(crate) fn new(manager: T, slot_count: usize) -> Result<Self> {
        if slot_count == 0 {
            return Err(EINVAL);
        }
        if slot_count > MAX_SLOTS {
            return Err(EINVAL);
        }
        // Since the slot index is stored in SeatInfo as a u8, the maximum number of slots is 256.
        if slot_count > u8::MAX as usize + 1 {
            return Err(EINVAL);
        }

        Ok(Self {
            manager,
            slot_count,
            slots: [const { Slot::Free }; MAX_SLOTS],
            use_seqno: 1,
        })
    }

    /// Records a newly activated slot for the given seat.
    /// The slot manager takes ownership of the hardware-specific slot data.
    fn record_active_slot(&mut self, slot_idx: usize, slot_data: T::SlotData) {
        let cur_seqno = self.use_seqno;

        *T::seat(&slot_data).access_mut(self) = Seat::Active(SeatInfo {
            slot: slot_idx as u8,
            seqno: cur_seqno,
        });

        self.slots[slot_idx] = Slot::Active(SlotInfo {
            slot_data,
            seqno: cur_seqno,
        });

        self.use_seqno += 1;
    }

    /// Reactivates an active/idle slot for a given seat without reprogramming the hardware.
    /// The SlotManager reuses the existing slot_data. This ensures that the hardware-specific
    /// information is not changed between subsequent uses. It also ensures that resources
    /// owned by the existing slot_data remain alive while the hardware is configured to use them.
    fn reactivate_slot(&mut self, slot_idx: usize, slot_data: &T::SlotData) -> Result {
        let cur_seqno = self.use_seqno;

        let mut slot_info = match mem::take(&mut self.slots[slot_idx]) {
            Slot::Active(slot_info) | Slot::Idle(slot_info) => slot_info,
            Slot::Free => {
                *T::seat(slot_data).access_mut(self) = Seat::NoSeat;
                return Err(EINVAL);
            }
        };

        *T::seat(slot_data).access_mut(self) = Seat::Active(SeatInfo {
            slot: slot_idx as u8,
            seqno: cur_seqno,
        });

        slot_info.seqno = cur_seqno;
        self.slots[slot_idx] = Slot::Active(slot_info);

        self.use_seqno += 1;

        Ok(())
    }

    /// Activates a slot for the given seat.
    fn activate_slot(&mut self, slot_idx: usize, slot_data: T::SlotData) -> Result {
        self.manager.activate(slot_idx, &slot_data)?;
        self.record_active_slot(slot_idx, slot_data);
        Ok(())
    }

    /// Finds a slot for the given seat. A free slot is preferred, but if none
    /// are available, the oldest idle slot is evicted and reused. Otherwise, if
    /// there are no free or idle slots, return [`EBUSY`].
    fn allocate_slot(&mut self, slot_data: T::SlotData) -> Result {
        let slots = &self.slots[..self.slot_count];

        let mut idle_slot_idx = None;
        let mut idle_slot_seqno: u64 = 0;

        for (slot_idx, slot) in slots.iter().enumerate() {
            match slot {
                Slot::Free => {
                    return self.activate_slot(slot_idx, slot_data);
                }
                Slot::Idle(slot_info) => {
                    if idle_slot_idx.is_none() || slot_info.seqno < idle_slot_seqno {
                        idle_slot_idx = Some(slot_idx);
                        idle_slot_seqno = slot_info.seqno;
                    }
                }
                Slot::Active(_) => (),
            }
        }

        match idle_slot_idx {
            Some(slot_idx) => {
                // Lazily evict idle slot just before it is reused.
                if let Slot::Idle(slot_info) = &self.slots[slot_idx] {
                    self.manager.evict(slot_idx, &slot_info.slot_data)?;
                    mem::take(&mut self.slots[slot_idx]);
                }
                self.activate_slot(slot_idx, slot_data)
            }
            None => Err(EBUSY),
        }
    }

    /// Converts an active slot and its seat to idle state.
    fn idle_slot(&mut self, slot_idx: usize, locked_seat: &LockedSeat<T, MAX_SLOTS>) -> Result {
        let slot = mem::take(&mut self.slots[slot_idx]);

        self.slots[slot_idx] = match slot {
            // If the slot was active, make it idle.
            Slot::Active(slot_info) => Slot::Idle(slot_info),

            // Preserve an already-idle slot.
            Slot::Idle(slot_info) => Slot::Idle(slot_info),

            // A free slot remains free.
            Slot::Free => Slot::Free,
        };

        // If the seat was active, make it idle, or keep it idle if it was already idle.
        *locked_seat.access_mut(self) = match locked_seat.access(self) {
            Seat::Active(seat_info) | Seat::Idle(seat_info) => Seat::Idle(SeatInfo {
                slot: seat_info.slot,
                seqno: seat_info.seqno,
            }),
            Seat::NoSeat => Seat::NoSeat,
        };
        Ok(())
    }

    /// Evicts an active or idle slot: calls the eviction callback and marks the slot as free
    /// and the seat as NoSeat.
    fn evict_slot(&mut self, slot_idx: usize, locked_seat: &LockedSeat<T, MAX_SLOTS>) -> Result {
        match &self.slots[slot_idx] {
            Slot::Active(slot_info) | Slot::Idle(slot_info) => {
                // If hardware eviction fails (e.g. times out), the slot retains
                // its SlotData so that any resources still referenced by the hardware
                // will remain alive. This prevents use-after-free errors.
                self.manager.evict(slot_idx, &slot_info.slot_data)?;
                mem::take(&mut self.slots[slot_idx]);
            }
            _ => (),
        }

        *locked_seat.access_mut(self) = Seat::NoSeat;
        Ok(())
    }

    /// Checks that the seat state matches the slot's state.
    /// If they don't match, the seat is stale and is reset to `NoSeat`.
    fn check_seat(&mut self, locked_seat: &LockedSeat<T, MAX_SLOTS>) {
        let (slot_idx, seat_seqno, is_active) = match locked_seat.access(self) {
            Seat::Active(seat_info) => (seat_info.slot as usize, seat_info.seqno, true),
            Seat::Idle(seat_info) => (seat_info.slot as usize, seat_info.seqno, false),
            _ => return,
        };

        let valid = if is_active {
            !kernel::warn_on!(!matches!(
                &self.slots[slot_idx],
                Slot::Active(slot_info) if slot_info.seqno == seat_seqno
            ))
        } else {
            matches!(
                &self.slots[slot_idx],
                Slot::Idle(slot_info) if slot_info.seqno == seat_seqno
            )
        };

        if !valid {
            *locked_seat.access_mut(self) = Seat::NoSeat;
        }
    }

    /// Activates a resource on any available/reclaimable slot.
    pub(crate) fn activate(&mut self, slot_data: T::SlotData) -> Result {
        self.check_seat(T::seat(&slot_data));

        // Copy out only the slot index so the borrow of slot_data ends here.
        let slot_idx = match T::seat(&slot_data).access(self) {
            Seat::Active(seat_info) | Seat::Idle(seat_info) => Some(seat_info.slot as usize),
            Seat::NoSeat => None,
        };

        match slot_idx {
            Some(slot_idx) => self.reactivate_slot(slot_idx, &slot_data),
            None => self.allocate_slot(slot_data),
        }
    }

    /// Flag a resource as idle. This method will be used for user VM support.
    #[expect(dead_code)]
    pub(crate) fn idle(&mut self, locked_seat: &LockedSeat<T, MAX_SLOTS>) -> Result {
        self.check_seat(locked_seat);
        if let Seat::Active(seat_info) = locked_seat.access(self) {
            self.idle_slot(seat_info.slot as usize, locked_seat)?;
        }
        Ok(())
    }

    /// Evict a resource from its slot.
    pub(crate) fn evict(&mut self, locked_seat: &LockedSeat<T, MAX_SLOTS>) -> Result {
        self.check_seat(locked_seat);

        match locked_seat.access(self) {
            Seat::Active(seat_info) | Seat::Idle(seat_info) => {
                let slot_idx = seat_info.slot as usize;
                self.evict_slot(slot_idx, locked_seat)?;
            }
            _ => (),
        }

        Ok(())
    }
}

impl<T: SlotOperations<MAX_SLOTS>, const MAX_SLOTS: usize> Deref for SlotManager<T, MAX_SLOTS> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.manager
    }
}

impl<T: SlotOperations<MAX_SLOTS>, const MAX_SLOTS: usize> DerefMut for SlotManager<T, MAX_SLOTS> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.manager
    }
}
