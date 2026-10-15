.. SPDX-License-Identifier: GPL-2.0

===========================================
Uso dos Módulos de Segurança do Linux (LSM)
===========================================

O framework dos Módulos de Segurança do Linux (LSM - Linux Security Module)
fornece um mecanismo para que várias verificações de segurança sejam interceptadas
por novas extensões do kernel. O nome "módulo" é um pouco impróprio, já que essas
extensões não são, na verdade, módulos carregáveis do kernel. Em vez disso, elas
são selecionáveis em tempo de compilação via CONFIG_DEFAULT_SECURITY e podem ser
substituídas em tempo de inicialização pelo argumento de linha de comando do
kernel ``"security=..."``, no caso em que múltiplos LSMs foram compilados em um
determinado kernel.

Os principais usuários da interface LSM são as extensões de Controle de Acesso
Obrigatório (MAC - Mandatory Access Control), que fornecem uma política de
segurança abrangente. Os exemplos incluem SELinux, Smack, Tomoyo e AppArmor. Além
das extensões MAC maiores, outras extensões podem ser construídas usando o LSM
para fornecer alterações específicas na operação do sistema quando esses ajustes
não estão disponíveis na funcionalidade principal do próprio Linux.

Os módulos de capacidades do Linux sempre serão incluídos. Isso pode ser seguido
por qualquer número de módulos "menores" (minor) e no máximo um módulo "maior"
(major). Para obter mais detalhes sobre capacidades, consulte ``capabilities(7)``
no projeto Linux man-pages.

Uma lista dos módulos de segurança ativos pode ser encontrada lendo
``/sys/kernel/security/lsm``. Esta é uma lista separada por vírgulas e sempre
incluirá o módulo de capacidade. A lista reflete a ordem na qual as verificações
são feitas. O módulo de capacidade sempre será o primeiro, seguido por quaisquer
módulos "menores" (por exemplo, Yama) e depois pelo módulo "maior" (por exemplo,
SELinux), se houver um configurado.

Os atributos de processo associados aos módulos de segurança "maiores" devem ser
acessados e mantidos usando os arquivos especiais em ``/proc/.../attr``. Um módulo
de segurança pode manter um subdiretório específico do módulo lá, nomeado após o
módulo. ``/proc/.../attr/smack`` é fornecido pelo módulo de segurança Smack e
contém todos os seus arquivos especiais. Os arquivos diretamente em
``/proc/.../attr`` permanecem como interfaces legadas para módulos que fornecem
subdiretórios.

Todolist:

*   apparmor
*   LoadPin
*   SELinux
*   Smack
*   tomoyo
*   Yama
*   SafeSetID
*   ipe
*   landlock
