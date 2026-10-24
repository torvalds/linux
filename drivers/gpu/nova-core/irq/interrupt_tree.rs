// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! The GIN CPU interrupt tree for one PCIe function.
//!
//! A [`GinVector`] names an interrupt source, a [`LeafIndex`] the leaf register that latches it,
//! a [`LeafMask`] a set of vectors within one leaf, and a [`Subtree`] one `TOP` bit. The types
//! keep the four from being confused with one another.
//!
//! Servicing a leaf requires reading its pending bits before clearing them. Only
//! [`Tree::read_pending`] produces a [`LeafPending`], and only a [`LeafPending`] clears a leaf,
//! so that the wrong order does not compile. This module does not serialize access to the tree.
//!
//! See `Documentation/gpu/nova/core/interrupts.rst`.

use kernel::{
    device::Bound,
    io::{
        register::Array,
        Io, //
    },
    irq,
    num::Bounded,
    pci::{
        self,
        IrqType, //
    },
    prelude::*, //
};

use crate::{
    driver::Bar0,
    gpu::Chipset,
    num, //
};

use super::{
    hal::{
        cpu_interrupt_hal,
        CpuInterruptHal, //
    },
    regs::*,
    MsiType, //
};

/// Number of vectors one leaf register carries, one per bit.
const VECTORS_PER_LEAF: u32 = u32::BITS;

/// Number of leaves one subtree covers.
const LEAVES_PER_SUBTREE: u32 = 2;

/// Number of subtrees the widest supported tree implements.
const MAX_NUM_SUBTREES: u32 = 8;

/// Number of leaves the widest supported tree implements.
const MAX_NUM_LEAVES: u32 = MAX_NUM_SUBTREES * LEAVES_PER_SUBTREE;

/// Number of bits needed to address every vector in the widest supported tree.
const VECTOR_BITS: u32 = (MAX_NUM_LEAVES * VECTORS_PER_LEAF).ilog2();

/// Width of the vector field in the leaf trigger register.
const TRIGGER_VECTOR_BITS: u32 = {
    let range = NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_TRIGGER::VECTOR_RANGE;
    num::u8_as_u32(*range.end() - *range.start() + 1)
};

/// Index of a leaf register within the widest supported tree. An 8-leaf tree implements only the
/// lower half of the range.
pub(super) type LeafIndex = Bounded<usize, { MAX_NUM_LEAVES.ilog2() }>;

/// Number of leaves a tree implements.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub(super) enum LeafCount {
    /// Turing through Ada.
    Eight = 8,

    /// Hopper and later.
    Sixteen = 16,
}

impl LeafCount {
    pub(super) const fn into_u32(self) -> u32 {
        // CAST: `LeafCount` is `repr(u32)`, so the cast is lossless.
        self as u32
    }

    pub(super) const fn into_raw(self) -> usize {
        num::u32_as_usize(self.into_u32())
    }

    /// Returns the number of subtrees a tree of this size implements.
    pub(super) const fn subtree_count(self) -> u32 {
        self.into_u32() / LEAVES_PER_SUBTREE
    }

    /// Returns the set of every subtree a tree of this size implements.
    pub(super) const fn subtree_set(self) -> SubtreeSet {
        SubtreeSet((1u32 << self.subtree_count()) - 1)
    }

    /// Returns the number of vectors a tree of this size carries.
    pub(super) const fn vector_count(self) -> u32 {
        self.into_u32() * VECTORS_PER_LEAF
    }

    /// Returns every leaf a tree of this size implements.
    pub(super) fn iter(self) -> impl Iterator<Item = LeafIndex> {
        (0..self.into_raw()).filter_map(LeafIndex::try_new)
    }
}

// `VECTOR_BITS` and `LeafCount::Sixteen` are written separately. This assert keeps them in
// agreement about the widest supported tree.
static_assert!(1 << VECTOR_BITS == LeafCount::Sixteen.vector_count());

/// Set of vectors within one leaf, one bit per vector.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct LeafMask(u32);

impl LeafMask {
    /// Returns the mask with every vector set.
    pub(super) const fn all() -> Self {
        Self(u32::MAX)
    }

    #[cfg_attr(not(CONFIG_NOVA_CORE_SELFTESTS), expect(dead_code))]
    pub(super) const fn from_raw(raw: u32) -> Self {
        Self(raw)
    }

    #[cfg_attr(
        not(any(CONFIG_NOVA_CORE_SELFTESTS, CONFIG_KUNIT = "y")),
        expect(dead_code)
    )]
    pub(super) const fn into_raw(self) -> u32 {
        self.0
    }

    pub(super) const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// Returns whether every vector in `other` is also in this mask.
    pub(super) const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }
}

impl From<Bounded<u32, 32>> for LeafMask {
    fn from(vectors: Bounded<u32, 32>) -> Self {
        Self(vectors.get())
    }
}

impl From<LeafMask> for Bounded<u32, 32> {
    fn from(vectors: LeafMask) -> Self {
        vectors.0.into()
    }
}

/// One subtree, held as the `TOP` bit that covers it.
///
/// # Invariants
///
/// Exactly one bit is set.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct Subtree(u32);

impl Subtree {
    /// Returns the subtree at index `idx`.
    const fn new(idx: u32) -> Self {
        // INVARIANT: shifting `1` left leaves exactly one bit set.
        Self(1 << idx)
    }

    /// Returns this subtree's index within the tree.
    pub(super) const fn index(self) -> u32 {
        self.0.trailing_zeros()
    }

    pub(super) const fn into_raw(self) -> u32 {
        self.0
    }
}

/// Set of subtrees, one bit per subtree, in the layout of the `TOP` registers.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct SubtreeSet(u32);

impl SubtreeSet {
    pub(super) const fn contains(self, subtree: Subtree) -> bool {
        self.0 & subtree.into_raw() != 0
    }

    pub(super) const fn is_empty(self) -> bool {
        self.0 == 0
    }

    pub(super) const fn intersection(self, other: Self) -> Self {
        Self(self.0 & other.0)
    }

    /// Returns one more than the highest index in this set, or `0` for an empty set. An MSI-X
    /// allocation that covers the set needs this many entries.
    pub(super) const fn span(self) -> u32 {
        u32::BITS - self.0.leading_zeros()
    }

    /// Returns the subtrees of this set, lowest index first.
    pub(super) fn iter(self) -> impl Iterator<Item = Subtree> {
        (0..u32::BITS)
            .map(Subtree::new)
            .filter(move |subtree| self.contains(*subtree))
    }
}

impl From<Subtree> for SubtreeSet {
    fn from(subtree: Subtree) -> Self {
        Self(subtree.into_raw())
    }
}

impl From<Bounded<u32, 32>> for SubtreeSet {
    fn from(subtrees: Bounded<u32, 32>) -> Self {
        Self(subtrees.get())
    }
}

impl From<SubtreeSet> for Bounded<u32, 32> {
    fn from(subtrees: SubtreeSet) -> Self {
        subtrees.0.into()
    }
}

/// A GIN interrupt vector, bounded to the widest tree that any supported chipset implements.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct GinVector(Bounded<u32, VECTOR_BITS>);

impl GinVector {
    /// Returns vector number `VECTOR`.
    ///
    /// Fails to compile if `VECTOR` is beyond the widest supported tree.
    pub(super) const fn new<const VECTOR: u32>() -> Self {
        Self(Bounded::<u32, VECTOR_BITS>::new::<VECTOR>())
    }

    #[cfg_attr(not(CONFIG_NOVA_CORE_SELFTESTS), expect(dead_code))]
    pub(super) const fn into_raw(self) -> u32 {
        self.0.get()
    }

    /// Returns this vector's leaf.
    pub(super) fn leaf_index(self) -> LeafIndex {
        // CALC: `self.0 / VECTORS_PER_LEAF`.
        self.0.shr::<{ VECTORS_PER_LEAF.ilog2() }, _>().cast()
    }

    /// Returns this vector's bit within its leaf.
    pub(super) const fn leaf_mask(self) -> LeafMask {
        LeafMask(1 << (self.0.get() % VECTORS_PER_LEAF))
    }

    /// Returns this vector's subtree.
    pub(super) const fn subtree(self) -> Subtree {
        Subtree::new(self.0.get() / (VECTORS_PER_LEAF * LEAVES_PER_SUBTREE))
    }

    /// Checks that a tree with `leaves` leaves implements this vector.
    ///
    /// # Errors
    ///
    /// `EINVAL` if it does not.
    pub(super) const fn validate(self, leaves: LeafCount) -> Result {
        if self.0.get() >= leaves.vector_count() {
            Err(EINVAL)
        } else {
            Ok(())
        }
    }
}

impl From<GinVector> for Bounded<u32, TRIGGER_VECTOR_BITS> {
    fn from(vector: GinVector) -> Self {
        vector.0.extend()
    }
}

/// Disables `vectors` in `leaf`.
fn clear_leaf_enables(bar: Bar0<'_>, leaf: LeafIndex, vectors: LeafMask) {
    bar.write(
        Array::at(*leaf),
        NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_EN_CLEAR::zeroed().with_vectors(vectors),
    );
}

/// Disables the subtrees in `serviced` at `TOP`.
fn clear_top_enables(bar: Bar0<'_>, serviced: SubtreeSet) {
    bar.write_reg(NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_CLEAR::zeroed().with_subtrees(serviced));
}

/// The CPU tree of one PCIe function, the subtrees that nova-core services, and the PCI vectors
/// that deliver them.
///
/// A subtree may be enabled at `TOP` only once a handler is registered on the PCI vector that
/// delivers it. See "The serviced-subtree invariant" in
/// `Documentation/gpu/nova/core/interrupts.rst`.
pub(crate) struct Tree<'a> {
    bar: Bar0<'a>,
    hal: &'static dyn CpuInterruptHal,
    serviced: SubtreeSet,
    /// The interrupt type that Linux granted for `vectors`.
    pub(super) msi_type: MsiType,
    vectors: pci::IrqVectorRegistration<'a>,
}

impl<'a> Tree<'a> {
    /// Creates the tree of `chipset` and allocates the PCI vectors for the subtrees in `serviced`.
    ///
    /// Requests MSI-X entries `0` through the highest subtree in `serviced`, since an allocation
    /// cannot be sparse, and falls back to a single MSI message for the whole tree.
    ///
    /// # Errors
    ///
    /// `EINVAL` if `serviced` is empty or names a subtree that `chipset` does not implement.
    /// Otherwise, when neither interrupt type could be allocated, the error from the MSI request.
    pub(crate) fn new(
        pdev: &'a pci::Device<Bound>,
        bar: Bar0<'a>,
        chipset: Chipset,
        serviced: SubtreeSet,
    ) -> Result<Self> {
        let hal = cpu_interrupt_hal(chipset);
        let num_leaves = hal.leaf_count();

        if serviced.is_empty() || serviced.intersection(num_leaves.subtree_set()) != serviced {
            return Err(EINVAL);
        }

        let entries = serviced.span();
        let (vectors, msi_type) = pdev
            .alloc_irq_vectors(entries, entries, IrqType::MsiX.into())
            .map(|vectors| (vectors, MsiType::MsiX))
            // Every subtree raises the one MSI message, so one MSI vector serves the whole tree.
            // See "Delivery over PCI" in `Documentation/gpu/nova/core/interrupts.rst`.
            .or_else(|_| {
                pdev.alloc_irq_vectors(1, 1, IrqType::Msi.into())
                    .map(|vectors| (vectors, MsiType::Msi))
            })?;

        Ok(Self {
            bar,
            hal,
            serviced,
            msi_type,
            vectors,
        })
    }

    /// Returns the [`irq::IrqRequest`] for the PCI vector that delivers `subtree`.
    ///
    /// # Errors
    ///
    /// `EINVAL` if `subtree` is not one of the serviced subtrees.
    pub(super) fn request_for(&self, subtree: Subtree) -> Result<irq::IrqRequest<'_>> {
        if !self.serviced.contains(subtree) {
            return Err(EINVAL);
        }

        let entry = match self.msi_type {
            MsiType::MsiX => num::u32_as_usize(subtree.index()),
            MsiType::Msi => 0,
        };

        self.vectors.index(entry).map(Into::into)
    }

    /// Rearms PCI interrupt delivery to the CPU after servicing `subtree`, the one subtree that
    /// the calling handler serves.
    ///
    /// A handler must call this before returning, or it receives no further interrupts.
    pub(super) fn rearm_pci_irq(&self, subtree: Subtree) {
        self.hal
            .rearm_pci_irq(self.bar, self.msi_type, self.serviced, subtree);
    }

    /// Enables the serviced subtrees at `TOP`.
    ///
    /// Each of them must have a handler registered on its PCI vector.
    pub(super) fn enable_top(&self) {
        self.bar.write_reg(
            NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_TOP_EN_SET::zeroed().with_subtrees(self.serviced),
        );
    }

    /// Disables the serviced subtrees at `TOP`.
    pub(super) fn disable_top(&self) {
        clear_top_enables(self.bar, self.serviced);
    }

    /// Enables the serviced subtrees at `TOP` until the returned guard drops.
    pub(crate) fn enable_top_guarded(&self) -> TopEnableGuard<'a> {
        self.enable_top();

        TopEnableGuard {
            bar: self.bar,
            serviced: self.serviced,
        }
    }

    /// Enables `vectors` in `leaf`.
    pub(super) fn enable_leaf(&self, leaf: LeafIndex, vectors: LeafMask) {
        self.bar.write(
            Array::at(*leaf),
            NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_EN_SET::zeroed().with_vectors(vectors),
        );
    }

    /// Disables `vectors` in `leaf`.
    pub(super) fn disable_leaf(&self, leaf: LeafIndex, vectors: LeafMask) {
        clear_leaf_enables(self.bar, leaf, vectors);
    }

    /// Enables `vectors` in `leaf` until the returned guard drops.
    pub(super) fn enable_leaf_guarded(
        &self,
        leaf: LeafIndex,
        vectors: LeafMask,
    ) -> LeafEnableGuard<'a> {
        self.enable_leaf(leaf, vectors);

        LeafEnableGuard {
            bar: self.bar,
            leaf,
            vectors,
        }
    }

    /// Reads the pending bits of `leaf`, and returns the handle that clears them.
    pub(super) fn read_pending(&self, leaf: LeafIndex) -> LeafPending<'a> {
        let pending = self
            .bar
            .read(NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF::at(*leaf))
            .vectors();

        LeafPending {
            bar: self.bar,
            leaf,
            pending,
        }
    }

    /// Sets the pending bit of `vector` in its leaf, as if the vector's source had raised it.
    ///
    /// # Errors
    ///
    /// `EINVAL` if this tree does not implement `vector`.
    #[cfg_attr(not(CONFIG_NOVA_CORE_SELFTESTS), expect(dead_code))]
    pub(super) fn trigger(&self, vector: GinVector) -> Result {
        vector.validate(self.hal.leaf_count())?;
        self.bar.write_reg(
            NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF_TRIGGER::zeroed().with_vector(vector),
        );

        Ok(())
    }

    /// Disables every vector in every implemented leaf, including the subtrees that nova-core does
    /// not service. Call this only during probe.
    pub(super) fn disable_all_leaves(&self) {
        for leaf in self.hal.leaf_count().iter() {
            self.disable_leaf(leaf, LeafMask::all());
        }
    }

    /// Clears every pending bit in every implemented leaf, including the subtrees that nova-core
    /// does not service.
    ///
    /// The serviced subtrees are disabled at `TOP` on return. Call this only during probe, with no
    /// interrupt handler registered.
    pub(super) fn drain(&self) {
        self.disable_top();

        // A vector that latched while disabled does not show in `TOP`, so read every leaf rather
        // than descending from it.
        for leaf in self.hal.leaf_count().iter() {
            self.read_pending(leaf).clear();
        }
    }

    /// Disables every vector, clears every pending bit, and rearms PCI interrupt delivery.
    ///
    /// The serviced subtrees are disabled at `TOP` on return. Call this only during probe, with
    /// no interrupt handler registered.
    pub(super) fn reset(&self) {
        self.disable_all_leaves();
        self.drain();
        for subtree in self.serviced.iter() {
            self.rearm_pci_irq(subtree);
        }

        // A `TOP`-enable rearm leaves the subtrees that it cycles enabled, and a subtree may be
        // enabled only once a handler is registered on its vector.
        self.disable_top();
    }
}

/// The pending bits of one leaf as they were read, and the handle that clears them.
pub(super) struct LeafPending<'a> {
    bar: Bar0<'a>,
    leaf: LeafIndex,
    pending: LeafMask,
}

impl LeafPending<'_> {
    pub(super) fn vectors(&self) -> LeafMask {
        self.pending
    }

    /// Clears the vectors that were pending at the read. A vector that latched since stays pending.
    pub(super) fn clear(&self) {
        self.clear_vectors(self.pending);
    }

    /// Clears `vectors` and no other bit.
    pub(super) fn clear_vectors(&self, vectors: LeafMask) {
        if !vectors.is_empty() {
            self.bar.write(
                Array::at(*self.leaf),
                NV_VIRTUAL_FUNCTION_PRIV_CPU_INTR_LEAF::zeroed().with_vectors(vectors),
            );
        }
    }
}

/// Disables a set of vectors in one leaf when dropped.
pub(super) struct LeafEnableGuard<'a> {
    bar: Bar0<'a>,
    leaf: LeafIndex,
    vectors: LeafMask,
}

impl Drop for LeafEnableGuard<'_> {
    fn drop(&mut self) {
        clear_leaf_enables(self.bar, self.leaf, self.vectors);
    }
}

/// Disables the serviced subtrees at `TOP` when dropped.
pub(crate) struct TopEnableGuard<'a> {
    bar: Bar0<'a>,
    serviced: SubtreeSet,
}

impl Drop for TopEnableGuard<'_> {
    fn drop(&mut self) {
        clear_top_enables(self.bar, self.serviced);
    }
}

#[kunit_tests(nova_core_gin_tree)]
mod tests {
    use super::*;

    /// A leaf index cannot name a leaf beyond the widest supported tree.
    #[test]
    fn leaf_index_bounds() {
        assert!(LeafIndex::try_new(0).is_some());
        assert!(LeafIndex::try_new(15).is_some());
        assert!(LeafIndex::try_new(16).is_none());
    }

    /// The subtree count, the implemented-subtree set, and the vector count follow the leaf count.
    #[test]
    fn leaf_count_derives_subtrees_and_vectors() {
        assert_eq!(LeafCount::Eight.subtree_count(), 4);
        assert_eq!(
            Bounded::<u32, 32>::from(LeafCount::Eight.subtree_set()).get(),
            0x0f
        );
        assert_eq!(LeafCount::Eight.vector_count(), 256);

        assert_eq!(LeafCount::Sixteen.subtree_count(), 8);
        assert_eq!(
            Bounded::<u32, 32>::from(LeafCount::Sixteen.subtree_set()).get(),
            0xff
        );
        assert_eq!(LeafCount::Sixteen.vector_count(), 512);
    }

    /// A tree enumerates every leaf that it implements, in order, and no more.
    #[test]
    fn leaf_count_iter_covers_the_tree() {
        for (count, expected) in [(LeafCount::Eight, 8usize), (LeafCount::Sixteen, 16)] {
            let mut seen = 0;

            for (index, leaf) in count.iter().enumerate() {
                assert_eq!(leaf.get(), index);
                seen += 1;
            }

            assert_eq!(seen, expected);
        }
    }

    /// A vector maps to its leaf, its bit within that leaf, and its subtree. The doorbell (129)
    /// and the GSP event (155) share a subtree.
    #[test]
    fn vector_maps_to_leaf_bit_and_subtree() {
        let doorbell = GinVector::new::<129>();
        let gsp = GinVector::new::<155>();

        assert_eq!(doorbell.leaf_index().get(), 4);
        assert_eq!(doorbell.leaf_mask().into_raw(), 1 << 1);
        assert_eq!(doorbell.subtree().index(), 2);

        assert_eq!(gsp.leaf_index().get(), 4);
        assert_eq!(gsp.leaf_mask().into_raw(), 1 << 27);
        assert_eq!(gsp.subtree().index(), 2);

        assert_eq!(doorbell.subtree(), gsp.subtree());
    }

    /// Both fixed vectors are within the 8-leaf tree, so every supported chipset implements them.
    #[test]
    fn fixed_vectors_fit_the_narrowest_tree() {
        assert!(GinVector::new::<129>().validate(LeafCount::Eight).is_ok());
        assert!(GinVector::new::<155>().validate(LeafCount::Eight).is_ok());

        // The first vector beyond an 8-leaf tree.
        assert!(GinVector::new::<256>().validate(LeafCount::Eight).is_err());
        assert!(GinVector::new::<256>().validate(LeafCount::Sixteen).is_ok());
    }

    /// A subtree set reports membership, intersection, and its span from subtree 0.
    #[test]
    fn subtree_set_operations() {
        let gsp = GinVector::new::<155>().subtree();

        assert!(LeafCount::Eight.subtree_set().contains(gsp));
        assert!(!LeafCount::Eight.subtree_set().is_empty());

        // The GSP needs no subtree above 2, so an MSI-X request covers entries 0 through 2.
        assert_eq!(SubtreeSet::from(gsp).span(), 3);

        // A 16-leaf tree implements every subtree that an 8-leaf tree does.
        assert_eq!(
            LeafCount::Sixteen
                .subtree_set()
                .intersection(LeafCount::Eight.subtree_set()),
            LeafCount::Eight.subtree_set()
        );
    }

    /// Iterating a subtree set yields each subtree once, lowest index first, and nothing for an
    /// empty set.
    #[test]
    fn subtree_set_iterates_its_members() {
        assert!(LeafCount::Eight
            .subtree_set()
            .iter()
            .map(Subtree::index)
            .eq([0u32, 1, 2, 3]));

        let gsp = SubtreeSet::from(GinVector::new::<155>().subtree());
        assert!(gsp.iter().map(Subtree::index).eq([2u32]));

        let empty = SubtreeSet::from(Bounded::<u32, 32>::new::<0>());
        assert_eq!(empty.iter().count(), 0);
    }

    /// Every supported chipset implements the subtree that carries the GSP event.
    #[test]
    fn gsp_subtree_is_implemented_everywhere() {
        for &chipset in Chipset::ALL {
            assert!(cpu_interrupt_hal(chipset)
                .leaf_count()
                .subtree_set()
                .contains(crate::irq::gsp::GSP_SUBTREE));
        }
    }
}
