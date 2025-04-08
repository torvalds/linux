.. include:: ../disclaimer-sp.rst

:Original: Documentation/process/submit-checklist.rst
:Translator: Avadhut Naik <avadhut.naik@amd.com>

.. _sp_submitchecklist:

Lista de comprobación para enviar parches del kernel de Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Aquí hay algunas cosas básicas que los desarrolladores deben hacer si
quieren que sus envíos de parches del kernel sean aceptados más
rápidamente.

Todo esto está más allá de la documentación que se proporciona en
:ref:`Documentation/translations/sp_SP/process/submitting-patches.rst <sp_submittingpatches>`
y en otros lugares con respecto al envío de parches del kernel de Linux.

1) Si utiliza una funcionalidad, #include el archivo que define/declara
   esa funcionalidad. No dependa de otros archivos de encabezado que
   extraigan los que utiliza.

2) Compile limpiamente:

  a) Con las opciones ``CONFIG`` aplicables o modificadas ``=y``, ``=m``,
     y ``=n``. Sin advertencias/errores del compilador ``gcc``, ni
     advertencias/errores del linker.

  b) Aprobar ``allnoconfig``, ``allmodconfig``

  c) Compila correctamente cuando se usa ``O=builddir``

  d) Cualquier documentación o cambios se compilan correctamente sin
     nuevas advertencias/errores. Utilice ``make htmldocs`` o
     ``make pdfdocs`` para comprobar la compilación y corregir cualquier
     problema.

3) Se compila en varias arquitecturas de CPU mediante herramientas de
   compilación cruzada locales o alguna otra granja de compilación.

4) ppc64 es una buena arquitectura para verificar la compilación cruzada
   por que tiende a usar ``unsigned long`` para cantidades de 64-bits.

5) Verifique su parche para el estilo general según se detalla en
   :ref:`Documentation/translations/sp_SP/process/coding-style.rst <sp_codingstyle>`.
   Verifique las infracciones triviales con el verificador de estilo de
   parches antes de la entrega (``scripts/checkpatch.pl``).
   Debería ser capaz de justificar todas las infracciones que permanezcan
   en su parche.

6) Cualquier opción ``CONFIG`` nueva o modificada no altera el menú de
   configuración y se desactiva por defecto, a menos que cumpla con los
   criterios de excepción documentados en
   ``Documentation/kbuild/kconfig-language.rst`` Atributos del menú: valor por defecto.

7) Todas las nuevas opciones de ``Kconfig`` tienen texto de ayuda.

8) Ha sido revisado cuidadosamente con respecto a las combinaciones
   relevantes de ``Kconfig``. Esto es muy difícil de hacer correctamente
   con las pruebas -- la concentración mental da resultados aquí.

9) Verifique limpiamente con sparse.

10) Use ``make checkstack`` y solucione cualquier problema que encuentre.

    .. note::

       ``checkstack`` no señala los problemas explícitamente, pero
       cualquier función que use más de 512 bytes en la pila es
       candidata para el cambio.

11) Incluya :ref:`kernel-doc <kernel_doc>` para documentar las API
    globales del kernel. (No es necesario para funciones estáticas, pero
    también está bien.) Utilice ``make htmldocs`` o ``make pdfdocs``
    para comprobar el :ref:`kernel-doc <kernel_doc>` y solucionar
    cualquier problema.

12) Ha sido probado con ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
    ``CONFIG_DEBUG_SLAB``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
    ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``
    ``CONFIG_PROVE_RCU`` y ``CONFIG_DEBUG_OBJECTS_RCU_HEAD`` todos
    habilitados simultáneamente.

13) Ha sido probado en tiempo de compilación y ejecución con y sin
    ``CONFIG_SMP`` y ``CONFIG_PREEMPT``.

14) Todas las rutas de código se han ejercido con todas las
    características de lockdep habilitadas.

15) Todas las nuevas entradas de ``/proc`` están documentadas en
    ``Documentation/``.

16) Todos los nuevos parámetros de arranque del kernel están documentados
    en ``Documentation/admin-guide/kernel-parameters.rst``.

17) Todos los nuevos parámetros del módulo están documentados con
    ``MODULE_PARM_DESC()``.

18) Todas las nuevas interfaces de espacio de usuario están documentadas
    en ``Documentation/ABI/``. Consulte Documentation/admin-guide/abi.rst
    (o ``Documentation/ABI/README``) para obtener más información.
    Los parches que cambian las interfaces del espacio de usuario deben
    ser CCed a linux-api@vger.kernel.org.

19) Se ha comprobado con la inyección de al menos errores de asignación
    de slab y página. Consulte ``Documentation/fault-injection/``.

    Si el nuevo código es sustancial, la adición de la inyección de
    errores específica del subsistema podría ser apropiada.

20) El nuevo código añadido ha sido compilado con ``gcc -W`` (use
    ``make KCFLAGS=-W``). Esto generara mucho ruido per es buena para
    encontrar errores como "warning: comparison between signed and unsigned".

21) Se prueba después de que se haya fusionado en el conjunto de
    parches -mm para asegurarse de que siga funcionando con todos los
    demás parches en cola y varios cambios en VM, VFS y otros subsistemas.

22) Todas las barreras de memoria {p.ej., ``barrier()``, ``rmb()``,
    ``wmb()``} necesitan un comentario en el código fuente que explique
    la lógica de lo que están haciendo y por qué.

23) Si se añaden algún ioctl en el parche, actualice también
    ``Documentation/userspace-api/ioctl/ioctl-number.rst``.

24) Si su código fuente modificado depende o utiliza cualquiera de las
    API o características del kernel que están relacionadas con los
    siguientes símbolos ``Kconfig`` entonces pruebe varias compilaciones
    con los símbolos ``Kconfig`` relacionados deshabilitados y/o ``=m``
    (si esa opción esta disponible) [no todos estos al mismo tiempo, solo
    varias/aleatorias combinaciones de ellos]:

    ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``, ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``
    ``CONFIG_NET``, ``CONFIG_INET=n`` (pero luego con ``CONFIG_NET=y``).
